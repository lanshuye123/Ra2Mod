#include <Misc/PromoteAura.h>

#include <Misc/SharedUtils.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

#include <YRPP.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace PromoteAura
{
	const char* const TypeName = "PromoteAura";

	namespace
	{
		// Ares' Action value for "this superweapon may be fired at a target".
		// Ares maps Action=Custom to it (Ares\src\Misc\Actions.h).
		constexpr unsigned AresAction_SuperWeaponAllowed = 0x7Fu;

		struct Params
		{
			double Range;              // cells
			int Levels;                // > 0 promotes, < 0 demotes
			unsigned Houses;           // HAresUtils::HouseFilter
			bool AffectBuildings;
			bool AffectAircraft;
			int MaxTargets;            // 0 = unlimited
			std::vector<TechnoTypeClass*> Types;   // empty = every type
			AnimTypeClass* pAnim;      // played on every affected techno
			int Sound;                 // VocClass index, -1 = none
		};

		std::unordered_map<const SuperWeaponTypeClass*, Params> Registry;
		bool Built = false;

		void ReadParams(const char* pSection, Params& params)
		{
			const auto pINI = CCINIClass::INI_Rules;

			params.Range = HAresUtils::ReadDouble(pINI, pSection, "PromoteAura.Range", 0.0);
			params.Levels = HAresUtils::ReadInt(pINI, pSection, "PromoteAura.Levels", 1);

			char buffer[0x400];

			if (HAresUtils::ReadString(pINI, pSection, "PromoteAura.Houses", buffer, sizeof(buffer)))
				params.Houses = HAresUtils::ParseHouseFilter(buffer, HAresUtils::HouseFilter_Owner);

			params.AffectBuildings = HAresUtils::ReadBool(pINI, pSection, "PromoteAura.AffectBuildings", false);
			params.AffectAircraft = HAresUtils::ReadBool(pINI, pSection, "PromoteAura.AffectAircraft", true);
			params.MaxTargets = HAresUtils::ReadInt(pINI, pSection, "PromoteAura.MaxTargets", 0);

			if (HAresUtils::ReadString(pINI, pSection, "PromoteAura.Types", buffer, sizeof(buffer)))
			{
				HAresUtils::ForEachToken(buffer, [&params, pSection](const char* pToken)
					{
						auto const pType = TechnoTypeClass::Find(pToken);

						if (!pType)
						{
							Debug::LogLine("[PromoteAura] %s: PromoteAura.Types names unknown type \"%s\"", pSection, pToken);
							return;
						}

						params.Types.push_back(pType);
					});
			}

			if (HAresUtils::ReadString(pINI, pSection, "PromoteAura.Anim", buffer, sizeof(buffer)))
			{
				const int index = AnimTypeClass::FindIndex(buffer);

				if (index >= 0)
					params.pAnim = AnimTypeClass::Array.GetItemOrDefault(index);
				else
					Debug::LogLine("[PromoteAura] %s: PromoteAura.Anim names unknown animation \"%s\"", pSection, buffer);
			}

			if (HAresUtils::ReadString(pINI, pSection, "PromoteAura.Sound", buffer, sizeof(buffer)))
			{
				params.Sound = VocClass::FindIndex(buffer);

				if (params.Sound < 0)
					Debug::LogLine("[PromoteAura] %s: PromoteAura.Sound names unknown sound \"%s\"", pSection, buffer);
			}
		}

		void Build()
		{
			Registry.clear();
			Built = false;

			const auto pINI = CCINIClass::INI_Rules;

			if (!pINI)
				return;   // rulesmd.ini is not loaded yet; try again later

			for (auto pSWType : SuperWeaponTypeClass::Array)
			{
				if (!pSWType || !pSWType->get_ID())
					continue;

				char typeName[0x40];

				if (!HAresUtils::ReadString(pINI, pSWType->get_ID(), "Type", typeName, sizeof(typeName)))
					continue;

				if (_stricmp(typeName, TypeName) != 0)
					continue;

				Params params
				{
					0.0,
					1,
					HAresUtils::HouseFilter_Owner,
					false,
					true,
					0,
					{},
					nullptr,
					-1
				};

				ReadParams(pSWType->get_ID(), params);

				// The engine has no idea what "Type=PromoteAura" means, so its
				// own dispatch table would either do nothing or run an unrelated
				// vanilla effect at the target cell. Forcing Invalid makes
				// SuperClass::Launch skip its effect dispatch completely while
				// leaving everything else (charging, cameo, sidebar, firing
				// sound) untouched. Ares guards this: its GetNewSWType() only
				// indexes its NewSWType array for indices >= FirstCustomType.
				pSWType->Type = SuperWeaponType::Invalid;

				Registry.emplace(pSWType, std::move(params));
			}

			Built = true;
		}

		void EnsureBuilt()
		{
			if (!Built)
				Build();
		}

		// Moves one whole rank per "level": rookie (0) <-> veteran (1) <-> elite (2).
		bool ApplyLevels(TechnoClass* pTechno, int levels)
		{
			auto& veterancy = pTechno->Veterancy;

			int current = 0;

			if (veterancy.IsElite())
				current = 2;
			else if (veterancy.IsVeteran())
				current = 1;

			int target = current + levels;

			if (target < 0)
				target = 0;

			if (target > 2)
				target = 2;

			if (target == current)
				return false;

			if (target == 0)
				veterancy.SetRookie(false);   // exactly 0.0, not the "negative rookie" -0.25
			else if (target == 1)
				veterancy.SetVeteran();
			else
				veterancy.SetElite();

			// CurrentRanking is deliberately left untouched: the engine (or Ares,
			// which replaces that block) detects the mismatch on the next update
			// and applies the new rank's stats, promotion sound and animation.
			return true;
		}

		bool IsAffectedType(const Params& params, TechnoClass* pTechno)
		{
			const bool isBuilding = pTechno->WhatAmI() == AbstractType::Building;

			if (isBuilding && !params.AffectBuildings)
				return false;

			if (!isBuilding && pTechno->IsInAir() && !params.AffectAircraft)
				return false;

			if (!params.Types.empty())
			{
				auto const pType = pTechno->GetTechnoType();

				if (std::find(params.Types.begin(), params.Types.end(), pType) == params.Types.end())
					return false;
			}

			return true;
		}

		// -------------------------------------------------------------------
		// Drop the "place superweapon" selection after firing.
		//
		// While a superweapon is selected the game keeps its index in
		// Unsorted::CurrentSWType (0x8809A0) and shows the superweapon cursor.
		// Every vanilla superweapon clears it in its own case handler inside
		// SuperClass::Launch (for example 0x6CD04F: MOV [0x8809A0], -1), and Ares
		// does the same for its NewSWType classes in SWTypeExt::Launch
		// (Ares\src\Ext\SWType\Body.cpp:528). A superweapon whose Type= the engine
		// does not know - ours - runs through neither path, so without this the
		// selection stays active and the superweapon cursor sticks after firing.
		//
		// The condition mirrors Ares: only drop the selection when it is this
		// superweapon's and it belongs to the player at this computer, so that an
		// AI or another player firing the same superweapon type cannot cancel what
		// the local player has selected.
		// -------------------------------------------------------------------
		void ClearSelectionAfterFiring(const SuperClass* pSuper)
		{
			if (!pSuper->Type || !pSuper->Owner || pSuper->Owner != HouseClass::CurrentPlayer)
				return;

			if (Unsorted::CurrentSWType != pSuper->Type->ArrayIndex)
				return;

			Unsorted::CurrentSWType = -1;

			Debug::LogLine("[PromoteAura] cleared the superweapon selection of %s (cursor returns to normal)",
				pSuper->Type->get_ID());
		}
	}

	void BuildRegistry()
	{
		Build();
	}

	bool IsPromoteAura(const SuperWeaponTypeClass* pSWType)
	{
		EnsureBuilt();
		return pSWType && Registry.find(pSWType) != Registry.end();
	}

	void LogRegistry()
	{
		EnsureBuilt();

		if (Registry.empty())
		{
			Debug::LogLine("[PromoteAura] no superweapon uses Type=%s", TypeName);
			return;
		}

		for (const auto& [pSWType, params] : Registry)
		{
			Debug::LogLine("[PromoteAura] %s: Range=%.2f Levels=%d Houses=0x%X Buildings=%d Aircraft=%d MaxTargets=%d Types=%d Anim=%s Sound=%d",
				pSWType->get_ID(), params.Range, params.Levels, params.Houses,
				params.AffectBuildings ? 1 : 0, params.AffectAircraft ? 1 : 0, params.MaxTargets,
				static_cast<int>(params.Types.size()),
				params.pAnim ? params.pAnim->get_ID() : "<none>", params.Sound);

			// Ares only lets a superweapon be fired at the map when its Action
			// is "SuperWeaponAllowed", which it derives from Action=Custom in
			// the superweapon's rulesmd.ini section.
			if (static_cast<unsigned>(pSWType->Action) != AresAction_SuperWeaponAllowed)
			{
				Debug::LogLine("[PromoteAura] %s: Action is not SuperWeaponAllowed - add \"Action=Custom\" to its rulesmd.ini section",
					pSWType->get_ID());
			}
		}
	}

	void Apply(SuperClass* pSuper, const CellStruct& cell)
	{
		EnsureBuilt();

		if (!pSuper || !pSuper->Type || !pSuper->Owner)
			return;

		const auto it = Registry.find(pSuper->Type);

		if (it == Registry.end())
			return;

		// The superweapon has been fired, so its selection is used up. This has
		// to happen before any early-out below: if the selection is not dropped,
		// the superweapon cursor stays active no matter what the aura does.
		ClearSelectionAfterFiring(pSuper);

		const Params& params = it->second;

		if (params.Range <= 0.0 || params.Levels == 0)
		{
			Debug::LogLine("[PromoteAura] %s fired without PromoteAura.Range or PromoteAura.Levels - nothing to do",
				pSuper->Type->get_ID());
			return;
		}

		auto const pCell = MapClass::Instance.GetCellAt(cell);

		if (!pCell)
			return;

		const CoordStruct target = pCell->GetCoords();
		int affected = 0;

		for (auto pTechno : TechnoClass::Array)
		{
			if (!pTechno || !pTechno->IsAlive || pTechno->Health <= 0 || pTechno->InLimbo)
				continue;

			if (!HAresUtils::MatchesHouseFilter(params.Houses, pSuper->Owner, pTechno->Owner))
				continue;

			if (!IsAffectedType(params, pTechno))
				continue;

			if (!HAresUtils::IsWithinRange(pTechno->GetCenterCoords(), target, params.Range))
				continue;

			if (!ApplyLevels(pTechno, params.Levels))
				continue;

			++affected;

			if (params.pAnim || params.Sound >= 0)
			{
				const CoordStruct coords = pTechno->GetCenterCoords();

				if (params.pAnim)
					GameCreate<AnimClass>(params.pAnim, coords);

				if (params.Sound >= 0)
					VocClass::PlayIndexAtPos(params.Sound, coords);
			}

			if (params.MaxTargets > 0 && affected >= params.MaxTargets)
				break;
		}

		Debug::LogLine("[PromoteAura] %s fired at cell (%d,%d): %d techno(s) %s by %d level(s)",
			pSuper->Type->get_ID(), cell.X, cell.Y, affected,
			params.Levels > 0 ? "promoted" : "demoted", params.Levels);
	}
}

// ---------------------------------------------------------------------------
//  SuperClass::Launch(CellStruct const& cell, bool isPlayer)
//
//  0x6CC390 is the start of the engine's launch routine (the first instruction
//  is SUB ESP,0x1D4, five bytes total with the two PUSHes that follow). ECX
//  holds the SuperClass, [ESP+4] the target cell and [ESP+8] the "fired by the
//  current player" flag.
//
//  Ares hooks this address as well, but it returns 0x6CDE40 (that function's
//  RET) only for superweapons backed by one of its NewSWType classes. An
//  unknown Type= makes Ares fall through, which is why HAres sits in the chain
//  here. Phobos also hooks this address and returns 0, so returning 0 keeps
//  everyone's hooks running.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x6CC390, HAres_SuperClass_Launch_PromoteAura, 0x6)
{
	GET(SuperClass*, pSuper, ECX);
	GET_STACK(const CellStruct*, pCell, 0x4);

	if (pSuper && pCell)
		PromoteAura::Apply(pSuper, *pCell);

	return 0;
}
