#include <HAres.h>

#include <Misc/UnitSuperWeapon.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>
#include <Utilities/Patch.h>

#include <YRPP.h>

#include <vector>

// ===========================================================================
//  Ares integration for unit-provided superweapons.
//
//  Why this cannot be a plain Syringe hook
//  ---------------------------------------
//  Ares 3.0 takes superweapon availability over completely. Its hooks at
//  0x50AF10 and 0x50B1D0 replace the engine's HouseClass::UpdateSuperWeapons*,
//  and its SuperClass::Lose hook at 0x6CB7B0 does the same - all three return a
//  non-zero address. Syringe only chains hooks that return 0, so a hook of ours
//  at any of those addresses is never reached while Ares is loaded.
//
//  What Ares actually does
//  -----------------------
//  Both of its hooks call one static helper:
//
//      std::vector<SWStatus>* GetSuperWeaponStatuses(HouseClass* pHouse)
//
//  which fills a function-static vector with one 3-byte entry per superweapon,
//  indexed by SuperWeaponTypeClass::ArrayIndex:
//
//      struct SWStatus { bool Available; bool PowerSourced; bool Charging; };
//
//  The only source it consults is pHouse->Buildings. The hooks then read that
//  vector to decide whether to Grant, Lose, SetOnHold or add the sidebar cameo.
//
//  The fix
//  -------
//  Redirect Ares' two calls into a wrapper that runs Ares' own computation and
//  then marks the superweapons our units provide as available. Everything
//  downstream stays Ares' code, so a unit-provided superweapon goes through
//  exactly the same charge, power, cameo, Grant and Lose paths as a building
//  one, and no state is ever fought over.
//
//  The addresses below were verified with Ghidra against this exact binary:
//    Ares.dll, FileVersion 20.333.289, PE TimeDateStamp 0x5fc37ef6
//    MD5 955a3977dfeaf7c76953a7c27d205dcd
// ===========================================================================

namespace
{
	// Mirrors Ares' SWStatus. Plain aggregate of bools, so sizeof == 3 and
	// element N lives at data() + N * 3 - which is exactly how the Ares hooks
	// index it ((char*)data + ArrayIndex * 3).
	struct AresSWStatus
	{
		bool Available;
		bool PowerSourced;
		bool Charging;
	};

	// One stack argument, pointer result: the vector lives in Ares as a function
	// -static, so there is no hidden return-value slot.
	//
	// The convention really is __stdcall, not __cdecl: Ares is compiled with /Gz,
	// so a plain free function defaults to __stdcall and cleans its own argument.
	// The function ends in `RET 0x4` (Ares+0x39260 / 0x3926C / 0x39285) and the
	// call sites have no `ADD ESP, 4` after the call. Getting this wrong corrupts
	// the stack by four bytes per call.
	using GetSWStatuses_t = std::vector<AresSWStatus>* (__stdcall*)(HouseClass*);

	GetSWStatuses_t AresGetSWStatuses = nullptr;
	bool AresIntegrationActive = false;

	// RVAs inside Ares.dll.
	struct AresOffsets
	{
		DWORD GetSWStatuses;
		DWORD CallSites[2];
	};

	constexpr AresOffsets Offsets_Ares30
	{
		0x00038F10,                 // GetSuperWeaponStatuses
		{ 0x0003945B, 0x000395A7 }  // in UpdateSuperWeaponsOwned / ...Unavailable
	};

	constexpr DWORD Timestamp_Ares30 = 0x5fc37ef6;
	constexpr DWORD Timestamp_Ares30p1 = 0x61daa114;

	std::vector<AresSWStatus>* __stdcall GetSWStatuses_Hook(HouseClass* pHouse)
	{
		// Let Ares build its own view first; it sizes and zeroes the vector.
		auto* const pStatuses = AresGetSWStatuses(pHouse);

		// Mirror the two cases in which Ares itself skips the whole scan.
		if (!pStatuses || !pHouse || pHouse->Defeated || pHouse == HouseClass::Observer)
			return pStatuses;

		for (int const index : UnitSuperWeapon::GetUnitProvidedIndices())
		{
			if (index < 0 || static_cast<size_t>(index) >= pStatuses->size())
				continue;

			if (!UnitSuperWeapon::IsProvidedByHouse(pHouse, index))
				continue;

			// Ares' own tail applies SWAllowed / DisableableFromShell and the
			// house power rule on top of this, so the superweapon ends up with
			// exactly the state a building provider would give it.
			auto& status = (*pStatuses)[static_cast<size_t>(index)];
			status.Available = true;
			status.PowerSourced = true;
			status.Charging = true;

			// One-shot proof in the log that a unit provider actually reached
			// Ares' availability computation.
			static std::vector<bool> Logged;

			if (static_cast<size_t>(index) >= Logged.size())
				Logged.resize(static_cast<size_t>(index) + 1, false);

			if (!Logged[static_cast<size_t>(index)])
			{
				Logged[static_cast<size_t>(index)] = true;

				auto const pSW = SuperWeaponTypeClass::Array.GetItemOrDefault(index);
				Debug::LogLine("[UnitSW] superweapon \"%s\" (index %d) made available by a unit provider%s",
					pSW ? pSW->ID : "?", index,
					pHouse->IsHumanPlayer ? " to the human player" : "");
			}
		}

		return pStatuses;
	}

	DWORD GetAresTimeDateStamp(HMODULE hAres)
	{
		const auto base = reinterpret_cast<DWORD>(hAres);
		const auto pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);

		if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
			return 0;

		const auto pNt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + pDos->e_lfanew);

		if (pNt->Signature != IMAGE_NT_SIGNATURE)
			return 0;

		return pNt->FileHeader.TimeDateStamp;
	}
}

namespace UnitSuperWeapon
{
	void InitAresIntegration()
	{
		const HMODULE hAres = GetModuleHandleA("Ares.dll");

		if (!hAres)
		{
			Debug::LogLine("[UnitSW] Ares.dll is not loaded. Unit-provided superweapons "
				"rely on Ares' availability rewrite, so the feature stays inactive.");
			return;
		}

		const DWORD stamp = GetAresTimeDateStamp(hAres);
		const AresOffsets* pOffsets = nullptr;
		const char* pVersionName = nullptr;

		if (stamp == Timestamp_Ares30)
		{
			pOffsets = &Offsets_Ares30;
			pVersionName = "3.0";
		}
		else if (stamp == Timestamp_Ares30p1)
		{
			pVersionName = "3.0p1";
		}
		else
		{
			Debug::LogLine("[UnitSW] unsupported Ares version (PE timestamp 0x%08X). "
				"Unit-provided superweapons stay inactive.", stamp);
			return;
		}

		if (!pOffsets)
		{
			// Ares 3.0p1 uses different offsets; they are not mapped yet.
			Debug::LogLine("[UnitSW] Ares %s detected, but its offsets are not mapped yet - "
				"feature stays inactive.", pVersionName);
			return;
		}

		const DWORD base = reinterpret_cast<DWORD>(hAres);

		AresGetSWStatuses = reinterpret_cast<GetSWStatuses_t>(base + pOffsets->GetSWStatuses);

		int patched = 0;

		for (DWORD const site : pOffsets->CallSites)
		{
			if (!site)
				continue;

			Patch::Apply_CALL(base + site, reinterpret_cast<DWORD>(&GetSWStatuses_Hook));
			++patched;

			Debug::LogLine("[UnitSW] redirected GetSuperWeaponStatuses call at Ares+0x%X", site);
		}

		AresIntegrationActive = patched > 0;

		Debug::LogLine("[UnitSW] Ares %s integration %s (%d call site(s) patched)",
			pVersionName, AresIntegrationActive ? "ACTIVE" : "FAILED", patched);
	}

	bool IsAresIntegrationActive()
	{
		return AresIntegrationActive;
	}
}
