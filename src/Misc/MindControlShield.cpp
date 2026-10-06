#include <Misc/MindControlShield.h>

#include <Misc/SharedUtils.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <YRPP.h>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace MindControlShield
{
	namespace
	{
		struct Params
		{
			double Range;                 // cells
			unsigned Houses;              // HAresUtils::HouseFilter
			bool ReleaseMindControl;
			bool BlockAttack;
		};

		// One live provider of a shield zone. Only plain data is kept, never a
		// TechnoClass pointer: the list outlives individual objects within a
		// frame, and a destroyed object's pointer would dangle.
		struct ActiveShield
		{
			CoordStruct Position;
			const Params* pParams;
			HouseClass* pOwner;
		};

		std::unordered_map<const TechnoTypeClass*, Params> Registry;
		std::vector<ActiveShield> ActiveShields;

		bool Built = false;
		bool AnyRelease = false;      // at least one type releases mind control
		bool AnyBlockAttack = false;  // at least one type blocks mind controlled firing
		int LastShieldFrame = -1;

		// The block itself sits on a hot path (every fire error query), so only
		// the first few of a match get a log line: enough to prove the feature in
		// a test run, not enough to spam a real game.
		constexpr int MaxBlockLogLines = 5;
		int BlockLogCount = 0;

		// Same idea for the release pass: the first few controlled units that turn
		// out to be outside every zone are logged, which answers "is the release
		// logic even seeing mind control".
		constexpr int MaxReleaseLogLines = 5;
		int ReleaseLogCount = 0;

		void Build()
		{
			Registry.clear();
			ActiveShields.clear();
			Built = false;
			AnyRelease = false;
			AnyBlockAttack = false;
			LastShieldFrame = -1;
			BlockLogCount = 0;
			ReleaseLogCount = 0;

			const auto pINI = CCINIClass::INI_Rules;

			if (!pINI)
				return;   // rulesmd.ini is not loaded yet; try again later

			for (auto pType : TechnoTypeClass::Array)
			{
				if (!pType || !pType->get_ID())
					continue;

				const char* pSection = pType->get_ID();
				const double range = HAresUtils::ReadDouble(pINI, pSection, "MindControlShield.Range", 0.0);

				if (range <= 0.0)
					continue;

				Params params
				{
					range,
					HAresUtils::HouseFilter_All,
					true,
					true
				};

				char buffer[0x100];

				if (HAresUtils::ReadString(pINI, pSection, "MindControlShield.Houses", buffer, sizeof(buffer)))
					params.Houses = HAresUtils::ParseHouseFilter(buffer, HAresUtils::HouseFilter_All);

				params.ReleaseMindControl = HAresUtils::ReadBool(pINI, pSection, "MindControlShield.ReleaseMindControl", true);
				params.BlockAttack = HAresUtils::ReadBool(pINI, pSection, "MindControlShield.BlockAttack", true);

				if (params.ReleaseMindControl)
					AnyRelease = true;

				if (params.BlockAttack)
					AnyBlockAttack = true;

				Registry.emplace(pType, params);
			}

			Built = true;
		}

		void EnsureBuilt()
		{
			if (!Built)
				Build();
		}

		void RebuildActiveShields()
		{
			ActiveShields.clear();

			if (Registry.empty())
				return;

			for (auto pTechno : TechnoClass::Array)
			{
				if (!pTechno || !pTechno->IsAlive || pTechno->Health <= 0 || pTechno->InLimbo || pTechno->Deactivated)
					continue;

				const auto it = Registry.find(pTechno->GetTechnoType());

				if (it == Registry.end())
					continue;

				// Note: the pointer stays valid across container growth, and the
				// registry is only rewritten between matches.
				ActiveShields.push_back(ActiveShield { pTechno->GetCenterCoords(), &it->second, pTechno->Owner });
			}
		}

		void EnsureFreshShields()
		{
			const int frame = Unsorted::CurrentFrame;

			if (frame == LastShieldFrame)
				return;

			LastShieldFrame = frame;
			RebuildActiveShields();
		}

		bool IsInsideShield(const CoordStruct& coords, HouseClass* pHouse, bool forRelease)
		{
			for (const auto& shield : ActiveShields)
			{
				const Params& params = *shield.pParams;

				if (forRelease ? !params.ReleaseMindControl : !params.BlockAttack)
					continue;

				if (!HAresUtils::MatchesHouseFilter(params.Houses, shield.pOwner, pHouse))
					continue;

				if (HAresUtils::IsWithinRange(coords, shield.Position, params.Range))
					return true;
			}

			return false;
		}

		// Frees every unit that is being mind controlled from inside a shield.
		// The original owner is taken from the control node, not from the unit:
		// a mind controlled unit is owned by its controller, and MindControlShield.Houses
		// is meant to select the side the unit *originally* belonged to.
		void ReleaseMindControlledUnits()
		{
			if (ActiveShields.empty())
				return;

			for (auto pManager : CaptureManagerClass::Array)
			{
				if (!pManager)
					continue;

				for (int i = pManager->ControlNodes.Count - 1; i >= 0; --i)
				{
					if (i >= pManager->ControlNodes.Count)
						continue;

					auto const pNode = pManager->ControlNodes[i];

					if (!pNode || !pNode->Unit)
						continue;

					TechnoClass* const pUnit = pNode->Unit;

					if (!pUnit->IsAlive || pUnit->Health <= 0 || pUnit->InLimbo)
						continue;

					// Read everything off the node *before* releasing: FreeUnit
					// deletes it (GameDelete(pNode)), so touching it afterwards
					// would be a use after free.
					HouseClass* const pOriginalOwner = pNode->OriginalOwner;
					const char* const pOriginalOwnerId = pOriginalOwner ? pOriginalOwner->get_ID() : "?";

					if (!IsInsideShield(pUnit->GetCenterCoords(), pOriginalOwner, true))
					{
						// Diagnostics: a controlled unit that is outside every zone
						// tells a modder that the release pass is running and that
						// the shield simply is not where the mind control happens.
						// Capped, like the block log.
						if (ReleaseLogCount < MaxReleaseLogLines)
						{
							++ReleaseLogCount;
							Debug::LogLine("[MindControlShield] %s is controlled (original owner %s) but outside every shield zone (log %d)",
								pUnit->get_ID(), pOriginalOwnerId, ReleaseLogCount);
						}

						continue;
					}

					// The engine's own release routine (replaced by Phobos'
					// superset when Phobos is loaded): uninit the ring anim, play
					// the mind-clear sound, restore the original owner, run
					// DecideUnitFate, clear MindControlledBy and drop the node.
					if (pManager->FreeUnit(pUnit))
					{
						Debug::LogLine("[MindControlShield] released %s (original owner %s) inside a shield zone",
							pUnit->get_ID(), pOriginalOwnerId);
					}
					else
					{
						Debug::LogLine("[MindControlShield] could not release %s: it has no control node in its controller's manager",
							pUnit->get_ID());
					}
				}
			}
		}
	}

	void BuildRegistry()
	{
		Build();
	}

	void LogRegistry()
	{
		EnsureBuilt();

		if (Registry.empty())
		{
			Debug::LogLine("[MindControlShield] no techno type configures MindControlShield.Range");
			return;
		}

		for (const auto& [pType, params] : Registry)
		{
			Debug::LogLine("[MindControlShield] %s: Range=%.2f Houses=0x%X ReleaseMindControl=%d BlockAttack=%d",
				pType->get_ID(), params.Range, params.Houses,
				params.ReleaseMindControl ? 1 : 0, params.BlockAttack ? 1 : 0);
		}
	}

	void Update()
	{
		EnsureBuilt();

		if (Registry.empty())
			return;

		EnsureFreshShields();

		if (AnyRelease)
			ReleaseMindControlledUnits();
	}

	bool BlocksFiring(TechnoClass* pAttacker, WeaponTypeClass* pWeapon)
	{
		EnsureBuilt();

		if (!AnyBlockAttack || !pAttacker || !pWeapon || !pWeapon->Warhead)
			return false;

		// Only mind control weapons are suppressed; everything else fires
		// normally out of a shield zone.
		if (!pWeapon->Warhead->MindControl)
			return false;

		EnsureFreshShields();

		if (!IsInsideShield(pAttacker->GetCenterCoords(), pAttacker->Owner, false))
			return false;

		if (BlockLogCount < MaxBlockLogLines)
		{
			++BlockLogCount;
			Debug::LogLine("[MindControlShield] %s may not fire mind control weapon %s from inside a shield zone (block %d)",
				pAttacker->get_ID(), pWeapon->get_ID(), BlockLogCount);
		}

		return true;
	}
}

// ---------------------------------------------------------------------------
//  TechnoClass::GetFireError(AbstractClass* pTarget, int nWeaponIndex, bool ignoreRange)
//  - the shared implementation at 0x6FC0B0.
//
//  0x6FC356 is `MOV AL, byte ptr [EDI+0x142]` (6 bytes), well past the prologue,
//  where the engine has already resolved the weapon for this shot:
//  EDI = WeaponTypeClass*, ESI = TechnoClass* pThis. Ares does not hook this
//  address and Phobos does not either, so it is free for HAres.
//
//  Returning 0x6FC0DF jumps straight to the function's shared epilogue that
//  sets EAX = FireError::REARM (3) and pops the prologue's registers, which is
//  exactly how Ares and Phobos veto a shot. Any other return value would have to
//  be a valid address of that shape, so the enum at the top of the hook lists
//  both useful exits.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6FC356, HAres_TechnoClass_GetFireError_MindControlShield, 0x6)
{
	enum
	{
		FireErrorRearm = 0x6FC0DF,   // FireError::REARM  - "not now", keeps the target
		FireErrorIllegal = 0x6FC86A  // FireError::ILLEGAL - "cannot fire at all"
	};

	GET(TechnoClass*, pThis, ESI);
	GET(WeaponTypeClass*, pWeapon, EDI);

	// REARM rather than ILLEGAL: the unit holds its target and simply cannot
	// shoot until it leaves the field, and the AI does not give the order up.
	if (MindControlShield::BlocksFiring(pThis, pWeapon))
		return FireErrorRearm;

	return 0;
}

// ---------------------------------------------------------------------------
//  TechnoClass::AI - once per techno per logic frame.
//
//  0x6F9E50 is the function's entry (SUB ESP,0x68 / PUSH EBX / PUSH EBP); ECX
//  holds the TechnoClass and the body moves it to ESI. Ares hooks it as
//  TechnoClass_Update and Phobos as TechnoClass_AI; both handlers return 0, so
//  HAres has to return 0 as well for the chain to survive.
//
//  The hook body does nothing per techno: MindControlShield::Update() is frame
//  guarded internally and only does real work once per logic frame, so this
//  stays a couple of comparisons on a hot path.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6F9E50, HAres_TechnoClass_AI_MindControlShield, 0x5)
{
	MindControlShield::Update();

	return 0;
}
