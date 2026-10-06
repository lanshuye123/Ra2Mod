#include "UnitSuperWeapon.h"

#include <Utilities/Debug.h>

#include <YRPP.h>

#include <cstring>
#include <vector>

namespace UnitSuperWeapon
{
	namespace
	{
		std::vector<Provider> Providers;

		// Distinct superweapon indices that have at least one unit provider.
		std::vector<int> SWIndices;

		// Splits a comma separated list and registers every entry as a provider of
		// pType. Returns the number of entries that were accepted.
		int RegisterList(TechnoTypeClass* pType, const char* pValue)
		{
			if (!pValue || !*pValue)
				return 0;

			char buffer[0x200];
			strncpy_s(buffer, pValue, _TRUNCATE);

			int accepted = 0;
			char* context = nullptr;

			for (char* token = strtok_s(buffer, ",", &context); token; token = strtok_s(nullptr, ",", &context))
			{
				while (*token == ' ' || *token == '\t')
					++token;

				char* end = token + strlen(token);

				while (end > token && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
					*--end = '\0';

				if (!*token)
					continue;

				const int index = SuperWeaponTypeClass::FindIndex(token);

				if (index < 0)
				{
					Debug::LogLine("[UnitSW] %s: ignoring unknown superweapon \"%s\"", pType->ID, token);
					continue;
				}

				Providers.push_back(Provider { pType, index });

				bool seen = false;

				for (int const known : SWIndices)
				{
					if (known == index)
					{
						seen = true;
						break;
					}
				}

				if (!seen)
					SWIndices.push_back(index);

				++accepted;
			}

			return accepted;
		}

		void RegisterType(TechnoTypeClass* pType)
		{
			if (!pType || !pType->ID || !*pType->ID)
				return;

			const auto pINI = CCINIClass::INI_Rules;

			if (!pINI)
				return;

			char value[0x200] {};
			int count = 0;

			// The same two fixed slots a building type has, plus Ares' list form.
			if (pINI->ReadString(pType->ID, "SuperWeapon", "", value, sizeof(value)) > 0)
				count += RegisterList(pType, value);

			if (pINI->ReadString(pType->ID, "SuperWeapon2", "", value, sizeof(value)) > 0)
				count += RegisterList(pType, value);

			if (pINI->ReadString(pType->ID, "SuperWeapons", "", value, sizeof(value)) > 0)
				count += RegisterList(pType, value);

			if (count)
				Debug::LogLine("[UnitSW] %s provides %d superweapon(s)", pType->ID, count);
		}
	}

	void BuildRegistry()
	{
		Providers.clear();
		SWIndices.clear();

		for (auto const pType : UnitTypeClass::Array)
			RegisterType(pType);

		for (auto const pType : InfantryTypeClass::Array)
			RegisterType(pType);

		for (auto const pType : AircraftTypeClass::Array)
			RegisterType(pType);

		Debug::LogLine("[UnitSW] registry built: %d provider(s), %d distinct superweapon(s)",
			static_cast<int>(Providers.size()), static_cast<int>(SWIndices.size()));
	}

	const std::vector<int>& GetUnitProvidedIndices()
	{
		return SWIndices;
	}

	bool IsProvidedByHouse(HouseClass* pHouse, int swIndex)
	{
		if (!pHouse || pHouse->Defeated)
			return false;

		for (auto const& provider : Providers)
		{
			if (provider.SWIndex != swIndex)
				continue;

			// "Active" counts mean owned and deployed on the map, which is the same
			// notion Ares uses for a building that can provide its superweapon.
			if (pHouse->CountOwnedAndPresent(provider.Type) > 0)
				return true;
		}

		return false;
	}

	void LogRegistry()
	{
		for (auto const& provider : Providers)
		{
			auto const pSW = SuperWeaponTypeClass::Array.GetItemOrDefault(provider.SWIndex);

			Debug::LogLine("[UnitSW]   %s -> %s",
				provider.Type->ID, pSW ? pSW->ID : "<invalid index>");
		}
	}

	void TickDiagnostics()
	{
		// Logs the human player's unit-provided superweapons whenever their
		// provided/granted/charged state changes. Together with Ares' own handling
		// this shows the whole lifecycle: the unit appears, the superweapon is
		// granted and starts charging, the unit is lost, the superweapon goes away.
		static std::vector<int> PrevState;

		auto const pHouse = HouseClass::CurrentPlayer;

		if (!pHouse)
			return;

		for (int const index : SWIndices)
		{
			if (index < 0 || !pHouse->Supers.ValidIndex(index))
				continue;

			auto const pSuper = pHouse->Supers[index];

			if (!pSuper)
				continue;

			const int provided = IsProvidedByHouse(pHouse, index) ? 1 : 0;
			const int granted = pSuper->IsPresent ? 1 : 0;
			const int charged = pSuper->IsReady ? 1 : 0;
			const int state = provided | (granted << 1) | (charged << 2);

			if (static_cast<size_t>(index) >= PrevState.size())
				PrevState.resize(static_cast<size_t>(index) + 1, -1);

			if (PrevState[static_cast<size_t>(index)] == state)
				continue;

			PrevState[static_cast<size_t>(index)] = state;

			Debug::LogLine("[UnitSW] \"%s\": providedByUnit=%d granted=%d charged=%d onHold=%d rechargeLeft=%d",
				pSuper->Type ? pSuper->Type->ID : "?", provided, granted, charged,
				pSuper->IsSuspended ? 1 : 0, pSuper->RechargeTimer.GetTimeLeft());
		}
	}
}
