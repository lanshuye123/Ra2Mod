#include <Misc/AresHelpers.h>

#include <Utilities/Debug.h>

// ---------------------------------------------------------------------------
//  Ares build table.
//
//  TimeDateStamp is the only version marker Ares exposes; it is the same value
//  Phobos keys its own Ares tables off (Phobos\src\Utilities\AresHelper.cpp).
//  The RVAs below come from Phobos' AresAddressInit.cpp:
//
//      InitAres3_0()    NOTE_ARES_FUN(ConvertTypeTo, 0x43650);
//      InitAres3_0p1()  NOTE_ARES_FUN(ConvertTypeTo, 0x44130);
//
//  Verified with Ghidra against the Ares.dll installed on this machine
//  (FileVersion 20.333.289, PE TimeDateStamp 0x5fc37ef6): 0x10043650 is the
//  routine that swaps a TechnoClass' type pointer and rescales its health.
// ---------------------------------------------------------------------------
namespace
{
	struct AresBuild
	{
		DWORD TimeDateStamp;
		const char* Name;
		DWORD ConvertTypeTo;   // RVA
	};

	constexpr AresBuild AresBuilds[] =
	{
		{ 0x5fc37ef6, "3.0",   0x00043650 },
		{ 0x61daa114, "3.0p1", 0x00044130 }
	};

	HMODULE AresModule = nullptr;
	DWORD AresBase = 0;
	DWORD AresTimeDateStamp = 0;
	const AresBuild* pAresBuild = nullptr;
	AresHelpers::ConvertTypeTo_t AresConvertTypeTo = nullptr;
	bool Initialized = false;

	DWORD ReadTimeDateStamp(HMODULE hModule)
	{
		if (!hModule)
			return 0;

		const auto base = reinterpret_cast<uintptr_t>(hModule);
		const auto pDos = reinterpret_cast<const PIMAGE_DOS_HEADER>(base);

		if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
			return 0;

		const auto pNt = reinterpret_cast<const PIMAGE_NT_HEADERS>(base + pDos->e_lfanew);

		if (pNt->Signature != IMAGE_NT_SIGNATURE)
			return 0;

		return pNt->FileHeader.TimeDateStamp;
	}
}

namespace AresHelpers
{
	void Init()
	{
		// Ares is injected before the game starts and its version never changes
		// while the process lives, so this only has to happen once.
		if (Initialized)
			return;

		Initialized = true;
		AresModule = GetModuleHandleA("Ares.dll");
		AresBase = reinterpret_cast<DWORD>(AresModule);
		AresTimeDateStamp = ReadTimeDateStamp(AresModule);
		pAresBuild = nullptr;
		AresConvertTypeTo = nullptr;

		if (!AresModule)
		{
			Debug::LogLine("[Ares] Ares.dll is not loaded. Features that build on Ares stay inactive.");
			return;
		}

		for (const auto& build : AresBuilds)
		{
			if (build.TimeDateStamp == AresTimeDateStamp)
			{
				pAresBuild = &build;
				break;
			}
		}

		if (!pAresBuild)
		{
			Debug::LogLine("[Ares] unsupported Ares version (PE TimeDateStamp 0x%08X). "
				"Ares-dependent features stay inactive.", AresTimeDateStamp);
			return;
		}

		AresConvertTypeTo = reinterpret_cast<ConvertTypeTo_t>(AresBase + pAresBuild->ConvertTypeTo);

		Debug::LogLine("[Ares] detected Ares %s at 0x%08X (ConvertTypeTo at 0x%08X)",
			pAresBuild->Name, AresBase, AresBase + pAresBuild->ConvertTypeTo);
	}

	HMODULE GetModule()
	{
		return AresModule;
	}

	DWORD GetTimeDateStamp()
	{
		return AresTimeDateStamp;
	}

	DWORD GetBase()
	{
		return AresBase;
	}

	const char* GetVersionName()
	{
		return pAresBuild ? pAresBuild->Name : "unknown";
	}

	bool IsSupported()
	{
		return pAresBuild != nullptr;
	}

	ConvertTypeTo_t GetConvertTypeTo()
	{
		return AresConvertTypeTo;
	}
}
