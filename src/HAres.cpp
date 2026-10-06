#include <HAres.h>

#include <Misc/AresHelpers.h>
#include <Misc/MindControlShield.h>
#include <Misc/PromoteAura.h>
#include <Misc/PromoteConvert.h>
#include <Misc/UnitSuperWeapon.h>
#include <Utilities/Debug.h>
#include <Utilities/Patch.h>

// Declares the SyringeFeatures namespace that Syringe/SyringeEx fills in.
#include <Syringe.h>

#include <cstdio>
#include <cstring>

HANDLE HAres::hInstance = nullptr;

// The leading L"" widens the narrow metadata literals it is concatenated with, so the
// name and version come from HAres.version.h instead of being spelled out twice.
const wchar_t* HAres::VersionDescription = L"" PRODUCT_NAME " " PRODUCT_VERSION " (" BUILD_TYPE_NAME ")";
const char* HAres::LogFileName = PRODUCT_NAME ".log";

bool HAres::Config_ShowWatermark = true;
bool HAres::Config_VerboseLog = false;
int  HAres::Config_WatermarkCorner = 0;

namespace
{
	// Absolute path of the directory containing the running executable, with a trailing
	// backslash. The working directory of an injected process is not reliable, so paths
	// are always derived from the module file name.
	void GetGameDirectory(char* pBuffer, size_t nSize)
	{
		if (!pBuffer || nSize == 0)
			return;

		pBuffer[0] = '\0';

		const DWORD len = GetModuleFileNameA(nullptr, pBuffer, static_cast<DWORD>(nSize));

		if (len == 0 || len >= nSize)
		{
			pBuffer[0] = '\0';
			return;
		}

		char* pLastSeparator = strrchr(pBuffer, '\\');

		if (pLastSeparator)
			*(pLastSeparator + 1) = '\0';
		else
			pBuffer[0] = '\0';
	}
}

void HAres::LoadConfig()
{
	char dir[MAX_PATH] {};
	GetGameDirectory(dir, sizeof(dir));

	char path[MAX_PATH] {};
	sprintf_s(path, "%s" PRODUCT_INI_NAME, dir);

	Config_ShowWatermark = GetPrivateProfileIntA("General", "ShowWatermark", 1, path) != 0;
	Config_VerboseLog = GetPrivateProfileIntA("General", "VerboseLog", 0, path) != 0;
	Config_WatermarkCorner = GetPrivateProfileIntA("General", "WatermarkCorner", 0, path);

	Debug::LogLine("[Config] file: %s", path[0] ? path : "<none>");
	Debug::LogLine("[Config] ShowWatermark=%d VerboseLog=%d WatermarkCorner=%d",
		Config_ShowWatermark ? 1 : 0, Config_VerboseLog ? 1 : 0, Config_WatermarkCorner);
}

void HAres::ExeRun()
{
	Debug::Init(LogFileName);

	Debug::LogLine("========================================================");
	Debug::LogLine("%s %s (%s)", PRODUCT_NAME, PRODUCT_VERSION, BUILD_TYPE_NAME);
	Debug::LogLine("Compiled " __DATE__ " " __TIME__);

	// SyringeEx sets these exported flags before installing any hooks; under the
	// original closed-source Syringe they stay false. HAres does not hard-fail on
	// them, but hooks that rely on SyringeEx behaviour should check them.
	Debug::LogLine("[Syringe] ESPModification=%d ZFPreservation=%d ReladdrInstructionFixup=%d",
		SyringeFeatures::ESPModification ? 1 : 0,
		SyringeFeatures::ZFPreservation ? 1 : 0,
		SyringeFeatures::ReladdrInstructionFixup ? 1 : 0);

	LoadConfig();

	// Applies every DEFINE_PATCH / DEFINE_JUMP that was compiled into the .patch section.
	Debug::LogLine("[Init] applying static patches");
	Patch::ApplyStatic();

	// Ares is injected by Syringe before the game starts, so it is already loaded
	// here. AresHelpers reads the module and its build once; every Ares-dependent
	// feature is version gated on that.
	AresHelpers::Init();

	// This installs the patch that lets units provide superweapons.
	UnitSuperWeapon::InitAresIntegration();

	// Promotion conversion observes rank changes through Ares' own rank update
	// helper, so its call sites have to be redirected here.
	PromoteConvert::InitAresIntegration();

	Debug::LogLine("[Init] complete");
}

void HAres::ExeTerminate()
{
	Debug::LogLine("[Exit] gamemd.exe is shutting down");
	Debug::Release();
}

void HAres::CmdLineParse(char** ppArgs, int nNumArgs)
{
	for (int i = 0; i < nNumArgs; ++i)
	{
		const char* pArg = ppArgs[i];

		if (!pArg)
			continue;

		Debug::LogLine("[CmdLine] arg[%d] = %s", i, pArg);

		// Example command line switch: -HAresVerbose
		if (_stricmp(pArg, "-HAresVerbose") == 0)
		{
			Config_VerboseLog = true;
			Debug::LogLine("[CmdLine] verbose logging enabled");
		}
	}
}

// The DLL entry point. Syringe injects this DLL with LoadLibrary, so this runs before
// any hook does; hInstance (the module base) is needed to locate the .patch section.
bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		HAres::hInstance = hInstance;
	}

	return true;
}
