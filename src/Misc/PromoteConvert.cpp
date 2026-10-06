#include <Misc/PromoteConvert.h>

#include <Misc/AresHelpers.h>
#include <Misc/SharedUtils.h>

#include <Utilities/Debug.h>
#include <Utilities/Macro.h>
#include <Utilities/Patch.h>

#include <YRPP.h>

#include <cstring>
#include <unordered_map>

namespace PromoteConvert
{
	namespace
	{
		struct Rule
		{
			TechnoTypeClass* VeteranType;   // Promote.VeteranType
			TechnoTypeClass* EliteType;     // Promote.EliteType
			bool KeepHealth;                // Promote.KeepHealth
			bool KeepVeterancy;             // Promote.KeepVeterancy
		};

		std::unordered_map<const TechnoTypeClass*, Rule> Registry;
		bool Built = false;
		bool AnyRule = false;   // at least one registered type

		// A handful of log lines that make "why did my unit not convert" answerable
		// from HAres.log, without filling the log of a long match.
		constexpr int MaxRankChangeLogLines = 8;
		int RankChangeLogCount = 0;

		// -------------------------------------------------------------------
		// Ares integration
		//
		// Ares' TechnoClass_Update_Veterancy hook (its handler at Ares+0x4F2E0,
		// hooked onto the game's 0x6FA054) returns 0x6FA14B, i.e. it replaces the
		// whole engine rank-change block. Every promotion therefore runs through
		// Ares' helper
		//
		//     void __stdcall AresRankUpdate(TechnoClass*, bool, bool)   // Ares+0x46AF0
		//
		// which is called from exactly three places (Ghidra cross references,
		// verified against the Ares 3.0 build whose PE TimeDateStamp is
		// 0x5fc37ef6):
		//
		//     0x4F2CF  TechnoClass_RegisterDestruction_Veterancy (kill rewards)
		//     0x4F2EB  TechnoClass_Update_Veterancy              (per frame)
		//     0x46B94  the helper's own recursive call for passengers
		//
		// Redirecting those three calls to a wrapper gives a clean "rank before /
		// rank after" observation point that runs *after* Ares has finished its
		// own promotion handling, including Ares' own Promote.*Type conversion.
		// -------------------------------------------------------------------
		using RankUpdate_t = void(__stdcall*)(TechnoClass*, bool, bool);

		constexpr DWORD RVA_AresRankUpdate = 0x00046AF0;
		constexpr DWORD AresRankUpdateCallSites[] =
		{
			0x0004F2CF,
			0x0004F2EB,
			0x00046B94
		};

		constexpr DWORD Timestamp_Ares30 = 0x5fc37ef6;

		RankUpdate_t AresRankUpdate = nullptr;
		bool AresIntegrationActive = false;

		// -------------------------------------------------------------------
		// INI
		// -------------------------------------------------------------------
		TechnoTypeClass* ReadTechnoType(CCINIClass* pINI, const char* pSection, const char* pKey)
		{
			char buffer[0x100];

			if (!HAresUtils::ReadString(pINI, pSection, pKey, buffer, sizeof(buffer)))
				return nullptr;

			auto const pType = TechnoTypeClass::Find(buffer);

			if (!pType)
				Debug::LogLine("[PromoteConvert] %s: %s names unknown type \"%s\"", pSection, pKey, buffer);

			return pType;
		}

		// A conversion may only change the type within one kind (infantry to
		// infantry, vehicle to vehicle, aircraft to aircraft): that is what the
		// engine and Ares' ConvertTypeTo support, and the underlying game class
		// stays the same object.
		bool IsCompatibleKind(const TechnoTypeClass* pFrom, const TechnoTypeClass* pTo)
		{
			if (!pFrom || !pTo || pFrom == pTo)
				return false;

			return pFrom->WhatAmI() == pTo->WhatAmI();
		}

		bool IsSupportedKind(const TechnoTypeClass* pType)
		{
			const auto kind = pType->WhatAmI();

			return kind == AbstractType::InfantryType
				|| kind == AbstractType::UnitType
				|| kind == AbstractType::AircraftType;
		}

		void ReadRule(const char* pSection, Rule& rule)
		{
			const auto pINI = CCINIClass::INI_Rules;

			rule.VeteranType = ReadTechnoType(pINI, pSection, "Promote.VeteranType");
			rule.EliteType = ReadTechnoType(pINI, pSection, "Promote.EliteType");
			rule.KeepHealth = HAresUtils::ReadBool(pINI, pSection, "Promote.KeepHealth", true);
			rule.KeepVeterancy = HAresUtils::ReadBool(pINI, pSection, "Promote.KeepVeterancy", true);
		}

		void Build()
		{
			Registry.clear();
			Built = false;
			AnyRule = false;
			RankChangeLogCount = 0;

			const auto pINI = CCINIClass::INI_Rules;

			if (!pINI)
				return;   // rulesmd.ini is not loaded yet; try again later

			for (auto pType : TechnoTypeClass::Array)
			{
				if (!pType || !pType->get_ID())
					continue;

				Rule rule { nullptr, nullptr, true, true };
				ReadRule(pType->get_ID(), rule);

				if (!rule.VeteranType && !rule.EliteType)
					continue;

				const char* pSection = pType->get_ID();

				if (!IsSupportedKind(pType))
				{
					Debug::LogLine("[PromoteConvert] %s: promotion conversion is only supported for infantry, vehicles and aircraft - ignored",
						pSection);
					continue;
				}

				auto Validate = [pSection, pType](TechnoTypeClass*& pTarget, const char* pKey)
					{
						if (!pTarget)
							return;

						if (!IsCompatibleKind(pType, pTarget))
						{
							Debug::LogLine("[PromoteConvert] %s: %s=%s must be a type of the same kind (infantry/vehicle/aircraft) - ignored",
								pSection, pKey, pTarget->get_ID());
							pTarget = nullptr;
						}
					};

				Validate(rule.VeteranType, "Promote.VeteranType");
				Validate(rule.EliteType, "Promote.EliteType");

				if (!rule.VeteranType && !rule.EliteType)
					continue;

				AnyRule = true;
				Registry.emplace(pType, rule);
			}

			Built = true;
		}

		void EnsureBuilt()
		{
			if (!Built)
				Build();
		}

		// -------------------------------------------------------------------
		// The conversion itself
		// -------------------------------------------------------------------
		void ApplyPolicy(TechnoClass* pThis, const Rule& rule)
		{
			// Ares' ConvertTypeTo already rescaled the health to keep the same
			// percentage, so only an explicit "reset" needs work here.
			if (!rule.KeepHealth)
			{
				pThis->SetHealthPercentage(1.0);
				pThis->EstimatedHealth = pThis->Health;
			}

			if (!rule.KeepVeterancy)
				pThis->Veterancy.Reset();

			// Keep the engine's cached rank in sync with what we just set, so no
			// spurious promotion/demotion is processed on the next update.
			pThis->CurrentRanking = pThis->Veterancy.GetRemainingLevel();
		}

		bool Convert(TechnoClass* pThis, TechnoTypeClass* pToType, const Rule& rule)
		{
			if (!pThis->IsAlive || pThis->InLimbo)
				return false;

			if (pThis->Transporter)
			{
				Debug::LogLine("[PromoteConvert] %s is inside a transport - conversion skipped", pThis->get_ID());
				return false;
			}

			auto const convert = AresHelpers::GetConvertTypeTo();

			if (!convert)
				return false;

			// Capture the source name and health before the swap: afterwards the
			// object already reports the new type, and the policy may have reset
			// the health on purpose.
			const char* const pSourceName = pThis->get_ID();
			const double healthBefore = pThis->GetHealthPercentage() * 100.0;

			if (!convert(pThis, pToType))
			{
				Debug::LogLine("[PromoteConvert] Ares refused to convert %s into %s", pSourceName, pToType->get_ID());
				return false;
			}

			ApplyPolicy(pThis, rule);

			Debug::LogLine("[PromoteConvert] %s -> %s (KeepHealth=%d KeepVeterancy=%d: health %.0f%% -> %.0f%%, rank %d)",
				pSourceName, pToType->get_ID(), rule.KeepHealth ? 1 : 0, rule.KeepVeterancy ? 1 : 0,
				healthBefore, pThis->GetHealthPercentage() * 100.0,
				static_cast<int>(pThis->Veterancy.GetRemainingLevel()));

			return true;
		}

		// pSourceType is the type the unit had *before* its rank changed, because
		// Ares may already have converted it by the time we look.
		void OnRankChanged(TechnoClass* pThis, TechnoTypeClass* pSourceType, Rank newRank)
		{
			const auto it = Registry.find(pSourceType);

			if (it == Registry.end())
				return;

			const Rule& rule = it->second;

			TechnoTypeClass* pToType = nullptr;

			if (newRank == Rank::Veteran)
				pToType = rule.VeteranType;
			else if (newRank == Rank::Elite)
				pToType = rule.EliteType;

			if (!pToType)
				return;

			// Ares reads the same two keys and converts first. In that case the
			// unit is already the target type and only the HAres policy (health
			// reset / star level) is applied on top - which is the normal path in
			// practice, so it is logged as well.
			if (pThis->GetTechnoType() == pToType)
			{
				const double healthBefore = pThis->GetHealthPercentage() * 100.0;
				const Rank rankBefore = pThis->Veterancy.GetRemainingLevel();

				ApplyPolicy(pThis, rule);

				Debug::LogLine("[PromoteConvert] %s was converted into %s by Ares - policy applied "
					"(KeepHealth=%d KeepVeterancy=%d: health %.0f%% -> %.0f%%, rank %d -> %d)",
					pSourceType->get_ID(), pToType->get_ID(), rule.KeepHealth ? 1 : 0, rule.KeepVeterancy ? 1 : 0,
					healthBefore, pThis->GetHealthPercentage() * 100.0,
					static_cast<int>(rankBefore), static_cast<int>(pThis->Veterancy.GetRemainingLevel()));
			}
			else
			{
				Convert(pThis, pToType, rule);
			}
		}

		void __stdcall RankUpdate_Hook(TechnoClass* pThis, bool silent, bool playPromotionEffects)
		{
			if (!pThis || !AnyRule)
			{
				AresRankUpdate(pThis, silent, playPromotionEffects);
				return;
			}

			// The rank the unit had *until now* is CurrentRanking, the value the
			// engine (and Ares) cache for exactly this comparison - not Veterancy,
			// which something else may already have written this frame. The range
			// superweapon is the obvious case: it sets Veterancy directly, so by
			// the time Ares processes it, "before" and "after" would look equal if
			// the float was used instead.
			// Rank::Invalid means the unit never had a rank applied yet (freshly
			// created); Ares skips its one-time promotion effects for that case,
			// and so does HAres.
			const Rank before = pThis->CurrentRanking;
			TechnoTypeClass* const pTypeBefore = pThis->GetTechnoType();

			AresRankUpdate(pThis, silent, playPromotionEffects);

			const Rank after = pThis->Veterancy.GetRemainingLevel();

			if (before != Rank::Invalid && after != before)
			{
				// Diagnostics: the first few rank changes are logged together with
				// whether that unit's type has a rule at all. This is what tells a
				// modder - and a test run - that the Ares call-site patch is live
				// and whether the promotion simply hit a type without rules.
				if (RankChangeLogCount < MaxRankChangeLogLines)
				{
					++RankChangeLogCount;

					Debug::LogLine("[PromoteConvert] %s changed rank %d -> %d (rule for this type: %s)",
						pTypeBefore->get_ID(), static_cast<int>(before), static_cast<int>(after),
						Registry.find(pTypeBefore) != Registry.end() ? "yes" : "no");
				}

				OnRankChanged(pThis, pTypeBefore, after);
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
			Debug::LogLine("[PromoteConvert] no techno type configures Promote.VeteranType or Promote.EliteType");
			return;
		}

		for (const auto& [pType, rule] : Registry)
		{
			Debug::LogLine("[PromoteConvert] %s: Veteran=%s Elite=%s KeepHealth=%d KeepVeterancy=%d",
				pType->get_ID(),
				rule.VeteranType ? rule.VeteranType->get_ID() : "<none>",
				rule.EliteType ? rule.EliteType->get_ID() : "<none>",
				rule.KeepHealth ? 1 : 0, rule.KeepVeterancy ? 1 : 0);
		}
	}

	void InitAresIntegration()
	{
		AresHelpers::Init();

		if (!AresHelpers::IsSupported())
		{
			Debug::LogLine("[PromoteConvert] promotion conversion needs Ares; feature stays inactive");
			return;
		}

		if (AresHelpers::GetTimeDateStamp() != Timestamp_Ares30)
		{
			Debug::LogLine("[PromoteConvert] Ares %s is not mapped yet (rank update call sites are known for 3.0 only) - "
				"feature stays inactive", AresHelpers::GetVersionName());
			return;
		}

		const DWORD base = AresHelpers::GetBase();
		AresRankUpdate = reinterpret_cast<RankUpdate_t>(base + RVA_AresRankUpdate);

		int patched = 0;

		for (DWORD const rva : AresRankUpdateCallSites)
		{
			Patch::Apply_CALL(base + rva, reinterpret_cast<DWORD>(&RankUpdate_Hook));
			++patched;

			Debug::LogLine("[PromoteConvert] redirected Ares rank update call at Ares+0x%X", rva);
		}

		AresIntegrationActive = patched > 0;

		Debug::LogLine("[PromoteConvert] Ares %s integration %s (%d call site(s) patched)",
			AresHelpers::GetVersionName(), AresIntegrationActive ? "ACTIVE" : "FAILED", patched);
	}
}
