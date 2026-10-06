#pragma once
#include <Windows.h>

class TechnoClass;
class TechnoTypeClass;

// ---------------------------------------------------------------------------
//  Access to the installed Ares.dll.
//
//  Ares 3.0 is closed source, so everything HAres needs from it is an RVA
//  pinned to a specific Ares build. Ares.dll exports its own hook functions,
//  but not the internal helpers, so the RVAs are taken from Phobos' tables
//  (Phobos\src\Utilities\AresAddressInit.cpp), which resolve the same
//  functions for the same two builds.
//
//  Every consumer must be version gated: when the running Ares build is not
//  mapped, the feature that depends on it has to switch itself off cleanly
//  instead of calling into an unknown address.
//
//  See docs/DEVELOPMENT.md sections 5.10 and 5.11 for how these addresses are
//  re-derived (export table + call target intersection + Ghidra cross-check).
// ---------------------------------------------------------------------------
namespace AresHelpers
{
	// Loaded Ares.dll, or null when Ares is not injected.
	HMODULE GetModule();

	// PE TimeDateStamp of Ares.dll, 0 when it could not be read.
	DWORD GetTimeDateStamp();

	// Image base of Ares.dll, 0 when Ares is not loaded.
	DWORD GetBase();

	// "3.0", "3.0p1" or "unknown" - for log lines only.
	const char* GetVersionName();

	// True when the running build is one HAres has addresses for.
	bool IsSupported();

	// Reads the module and its version. Safe to call more than once.
	void Init();

	// --- Ares' internal type conversion -------------------------------------
	// bool __stdcall ConvertTypeTo(TechnoClass*, TechnoTypeClass*)
	//
	// This is the engine-equivalent "turn this object into another type"
	// routine: it swaps the type pointer, rescales the health by percentage,
	// clamps the ammo, refreshes the facing ROT, replaces the locomotor and
	// recalculates the stats. Phobos' TechnoExt::ConvertToType delegates to it
	// whenever Ares is present.
	//
	// Returns null when Ares is missing or its build is not mapped.
	using ConvertTypeTo_t = bool(__stdcall*)(TechnoClass*, TechnoTypeClass*);
	ConvertTypeTo_t GetConvertTypeTo();
}
