#pragma once
#include <HAres.version.h>
#include <Windows.h>

// Global state of the HAres extension DLL.
//
// HAres is a Syringe/SyringeEx hook DLL that is injected into gamemd.exe next to
// Ares.dll and Phobos.dll. It deliberately keeps no game types in this header so
// that non-game code (config, logging) does not need to pull in YRpp.
class HAres
{
public:
	// HMODULE of this DLL as handed to DllMain.
	static HANDLE hInstance;

	// Wide version banner; drawn on screen and written to the log.
	static const wchar_t* VersionDescription;

	// Log file name, created next to gamemd.exe.
	static const char* LogFileName;

	// --- configuration, read from HAres.ini next to gamemd.exe ---
	static bool Config_ShowWatermark;
	static bool Config_VerboseLog;
	static int  Config_WatermarkCorner;

	// Reads HAres.ini. Safe to call more than once.
	static void LoadConfig();

	// Lifecycle, driven by the hooks in src\Misc\Hooks.Demo.cpp.
	static void ExeRun();
	static void ExeTerminate();
	static void CmdLineParse(char** ppArgs, int nNumArgs);
};
