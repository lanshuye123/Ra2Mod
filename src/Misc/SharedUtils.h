#pragma once

// Small helpers shared by the HAres feature modules.
//
// This is an internal header for code that already works with game types, so it
// pulls in YRpp on purpose. Non-game code (HAres.cpp, logging, config) must not
// include it - see the layering rule in docs/DEVELOPMENT.md section 4.

#include <YRPP.h>

#include <Utilities/Debug.h>

#include <cstring>

namespace HAresUtils
{
	// -----------------------------------------------------------------------
	// House filters
	//
	// "Whose stuff does this effect apply to", written in INI as a comma
	// separated list of owner / allies / enemies / all / none. The semantics
	// mirror Phobos' AffectedHouse (EnumFunctions::CanTargetHouse): "allies"
	// includes the owner itself, and "enemies" is everybody the owner is not
	// allied with.
	// -----------------------------------------------------------------------
	enum HouseFilter : unsigned
	{
		HouseFilter_None = 0u,
		HouseFilter_Owner = 1u << 0,
		HouseFilter_Allies = 1u << 1,
		HouseFilter_Enemies = 1u << 2,
		HouseFilter_All = HouseFilter_Owner | HouseFilter_Allies | HouseFilter_Enemies
	};

	// Returns nDefault when the value is empty or contains no known token.
	inline unsigned ParseHouseFilter(const char* pValue, unsigned nDefault)
	{
		if (!pValue || !*pValue)
			return nDefault;

		char buffer[0x100];
		strncpy_s(buffer, pValue, _TRUNCATE);

		unsigned filter = HouseFilter_None;
		bool anyToken = false;
		char* pContext = nullptr;

		for (char* pToken = strtok_s(buffer, ",", &pContext); pToken; pToken = strtok_s(nullptr, ",", &pContext))
		{
			while (*pToken == ' ' || *pToken == '\t')
				++pToken;

			char* pEnd = pToken + strlen(pToken);

			while (pEnd > pToken && (pEnd[-1] == ' ' || pEnd[-1] == '\t'))
				*--pEnd = '\0';

			if (!*pToken)
				continue;

			if (!_stricmp(pToken, "owner"))
				filter |= HouseFilter_Owner;
			else if (!_stricmp(pToken, "allies") || !_stricmp(pToken, "ally"))
				filter |= HouseFilter_Allies;
			else if (!_stricmp(pToken, "enemies") || !_stricmp(pToken, "enemy"))
				filter |= HouseFilter_Enemies;
			else if (!_stricmp(pToken, "all"))
				filter |= HouseFilter_All;
			else if (!_stricmp(pToken, "none"))
				filter |= HouseFilter_None;
			else
			{
				Debug::LogLine("[HAresUtils] unknown house filter token \"%s\" (expected owner, allies, enemies, all or none)", pToken);
				continue;
			}

			anyToken = true;
		}

		return anyToken ? filter : nDefault;
	}

	// pReference is the house owning the object that carries the filter (the
	// shield or the superweapon), pHouse is the house being tested.
	inline bool MatchesHouseFilter(unsigned nFilter, HouseClass* pReference, HouseClass* pHouse)
	{
		if (!nFilter || !pReference || !pHouse)
			return false;

		if ((nFilter & HouseFilter_Owner) && pReference == pHouse)
			return true;

		if (nFilter & HouseFilter_Allies)
		{
			if (pReference->IsAlliedWith(pHouse))
				return true;
		}

		if ((nFilter & HouseFilter_Enemies) && !pReference->IsAlliedWith(pHouse))
			return true;

		return false;
	}

	// -----------------------------------------------------------------------
	// Ranges
	//
	// Every range in HAres is written in map cells. Distances deliberately
	// ignore the Z axis: the game's own range logic is effectively 2D, and
	// including Z would make an aircraft at altitude immune to a ground aura.
	// -----------------------------------------------------------------------
	inline double LeptonRange(double rangeInCells)
	{
		return rangeInCells * static_cast<double>(Unsorted::LeptonsPerCell);
	}

	inline double DistanceSquared2D(const CoordStruct& a, const CoordStruct& b)
	{
		const double dx = static_cast<double>(a.X - b.X);
		const double dy = static_cast<double>(a.Y - b.Y);
		return dx * dx + dy * dy;
	}

	inline bool IsWithinRange(const CoordStruct& a, const CoordStruct& b, double rangeInCells)
	{
		if (rangeInCells <= 0.0)
			return false;

		const double maxDistance = LeptonRange(rangeInCells);
		return DistanceSquared2D(a, b) <= maxDistance * maxDistance;
	}

	// -----------------------------------------------------------------------
	// INI helpers
	// -----------------------------------------------------------------------
	// Invokes action(pToken) for every non-empty token of a comma separated
	// list, with surrounding whitespace removed. pToken is only valid inside
	// the call.
	template <typename Func>
	void ForEachToken(const char* pValue, Func&& action)
	{
		if (!pValue || !*pValue)
			return;

		char buffer[0x400];
		strncpy_s(buffer, pValue, _TRUNCATE);

		char* pContext = nullptr;

		for (char* pToken = strtok_s(buffer, ",", &pContext); pToken; pToken = strtok_s(nullptr, ",", &pContext))
		{
			while (*pToken == ' ' || *pToken == '\t')
				++pToken;

			char* pEnd = pToken + strlen(pToken);

			while (pEnd > pToken && (pEnd[-1] == ' ' || pEnd[-1] == '\t' || pEnd[-1] == '\r' || pEnd[-1] == '\n'))
				*--pEnd = '\0';

			if (*pToken)
				action(pToken);
		}
	}

	// Convenience wrappers around the game's INI reader so that the features do
	// not repeat the buffer dance. pSection is a TechnoType / SuperWeaponType ID.
	inline bool ReadString(CCINIClass* pINI, const char* pSection, const char* pKey, char* pBuffer, size_t nSize)
	{
		if (!pINI || !pSection || !pKey || !pBuffer || nSize == 0)
			return false;

		pBuffer[0] = '\0';
		return pINI->ReadString(pSection, pKey, "", pBuffer, nSize) > 0;
	}

	inline bool ReadBool(CCINIClass* pINI, const char* pSection, const char* pKey, bool bDefault)
	{
		return pINI->ReadBool(pSection, pKey, bDefault);
	}

	inline int ReadInt(CCINIClass* pINI, const char* pSection, const char* pKey, int nDefault)
	{
		return pINI->ReadInteger(pSection, pKey, nDefault);
	}

	inline double ReadDouble(CCINIClass* pINI, const char* pSection, const char* pKey, double dDefault)
	{
		return pINI->ReadDouble(pSection, pKey, dDefault);
	}
}
