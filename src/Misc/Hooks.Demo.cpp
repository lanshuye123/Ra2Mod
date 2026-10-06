#include <HAres.h>

#include <Misc/UnitSuperWeapon.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>
#include <Utilities/Patch.h>

#include <YRPP.h>

#include <cstring>

// ===========================================================================
//  HAres hook collection.
//
//  Every DEFINE_HOOK(address, name, size) below does two things:
//
//    1. it emits a `hookdecl` record into the .syhks00 PE section, which Syringe
//       reads *before* the game starts so it knows where to plant its 5-byte JMP;
//    2. it defines the exported handler that the injected JMP lands in.
//
//  `size` is the number of bytes of *original instructions* the hook steals. It is
//  not the size of the jump. When several DLLs hook the same address, Syringe chains
//  them and uses the largest declared size.
//
//  Return value semantics:
//
//    return 0;         -> run the stolen instructions, then continue at
//                         address + size. In a chain this means "invoke the next
//                         hook at this address".
//    return 0xADDRESS; -> jump straight to ADDRESS, skipping the stolen bytes.
//                         Use this to replace or shortcut the original logic.
//    return R->Origin() + offset;
//                      -> jump relative to the hook address; needed when one handler
//                         serves several addresses through DEFINE_HOOK_AGAIN.
//
//  All addresses below are absolute addresses inside gamemd.exe (image base
//  0x400000). They must start on an instruction boundary.
// ===========================================================================

namespace HAresDemo
{
	// -----------------------------------------------------------------------
	// Patch machinery smoke test.
	//
	// This deliberately writes back the bytes it just read, so it changes nothing.
	// Its only purpose is to prove that Patch::Apply and the VirtualProtect dance
	// work before you rely on them for a real patch. A real patch looks like:
	//
	//     // NOP out a conditional jump so a branch is always taken
	//     DEFINE_PATCH(0x123456, 0x90, 0x90);
	//
	//     // Replace a call target
	//     DEFINE_JUMP(CALL, 0x123456, 0x654321);
	//
	//     // Replace a virtual function pointer
	//     DEFINE_JUMP(VTABLE, 0x7E2280, &MyHandler);
	//
	// Static patches are applied by Patch::ApplyStatic() during ExeRun. Use
	// Patch::Apply_* directly when the decision depends on runtime state.
	// -----------------------------------------------------------------------
	void DemonstrateNoOpPatch()
	{
		constexpr DWORD Address = 0x7CD810;
		constexpr size_t Count = 4;

		byte original[Count] {};
		memcpy(original, reinterpret_cast<const void*>(static_cast<uintptr_t>(Address)), Count);

		Debug::LogLine("[Patch] bytes at 0x%06X: %02X %02X %02X %02X (writing back unchanged)",
			Address, original[0], original[1], original[2], original[3]);

		Patch::Apply_RAW(Address, { original[0], original[1], original[2], original[3] });
	}
}

// ---------------------------------------------------------------------------
// 0x7CD810 - the game's WinMain. The single best place for one-time init: the
// process exists and the game directory is current, but no game state is built yet.
//
// Also hooked by Ares and Phobos; because return 0 hands over to the next hook in
// the chain, all three run.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x7CD810, HAres_ExeRun, 0x9)
{
	HAres::ExeRun();
	return 0;
}

// ---------------------------------------------------------------------------
// 0x52F639 - command line parsing. ESI holds char** argv, EDI the argument count.
// Register contents are always context specific: verify them in the disassembly
// before trusting them.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x52F639, HAres_YR_CmdLineParse, 0x5)
{
	GET(char**, ppArgs, ESI);
	GET(int, nNumArgs, EDI);

	HAres::CmdLineParse(ppArgs, nNumArgs);

	if (HAres::Config_VerboseLog)
		HAresDemo::DemonstrateNoOpPatch();

	return 0;
}

// ---------------------------------------------------------------------------
// 0x4F4583 - GScreenClass text drawing, called once per rendered frame. Hooking a
// per-frame function is how overlays and HUD additions are implemented. Keep the
// body cheap: it runs at frame rate.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x4F4583, HAres_GScreenClass_DrawText, 0x6)
{
	if (!HAres::Config_ShowWatermark)
		return 0;

	auto const pSurface = DSurface::Composite;

	if (!pSurface)
		return 0;

	// One-shot proof that per-frame hooks are live.
	static bool s_DrawTextLogged = false;

	if (!s_DrawTextLogged)
	{
		s_DrawTextLogged = true;
		Debug::LogLine("[Hook] GScreenClass::DrawText fired (surface %dx%d)",
			pSurface->GetWidth(), pSurface->GetHeight());
	}

	UnitSuperWeapon::TickDiagnostics();

	auto const wanted = Drawing::GetTextDimensions(HAres::VersionDescription, { 0, 0 }, 0, 2, 0);

	const int marginX = 10;
	const int marginY = 10;
	int x = marginX;
	int y = marginY;

	switch (HAres::Config_WatermarkCorner)
	{
	case 1: // top-right
		x = pSurface->GetWidth() - wanted.Width - marginX;
		break;
	case 2: // bottom-left
		y = pSurface->GetHeight() - wanted.Height - marginY;
		break;
	case 3: // bottom-right
		x = pSurface->GetWidth() - wanted.Width - marginX;
		y = pSurface->GetHeight() - wanted.Height - marginY;
		break;
	default: // top-left, so that the Phobos watermark in the top-right stays readable
		break;
	}

	RectangleStruct rect = { x - 5, y - 5, wanted.Width + 10, wanted.Height + 10 };
	Point2D location { x, y };

	pSurface->FillRect(&rect, COLOR_BLACK);
	pSurface->DrawText(HAres::VersionDescription, &location, COLOR_YELLOW);

	return 0;
}

// ---------------------------------------------------------------------------
// 0x683E7F - ScenarioClass::Start, i.e. "a match is being set up". This is the
// conventional place to apply per-match logic and to read rulesmd.ini-derived data.
//
// Reaching this hook proves the DLL works far beyond startup.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x683E7F, HAres_ScenarioClass_Start, 0x7)
{
	static int s_ScenarioStarts = 0;
	++s_ScenarioStarts;

	Debug::LogLine("[Hook] ScenarioClass::Start #%d", s_ScenarioStarts);

	// rulesmd.ini is loaded by now, so this is the earliest point at which the
	// SuperWeapon= keys on unit types can be read.
	UnitSuperWeapon::BuildRegistry();
	UnitSuperWeapon::LogRegistry();

	auto const pScenario = ScenarioClass::Instance;

	if (pScenario)
		Debug::LogLine("[Hook]   FileName=\"%s\"", pScenario->FileName);

	auto const pRules = RulesClass::Instance;

	if (pRules)
		Debug::LogLine("[Hook]   Rules loaded, MessageDelay=%d", pRules->MessageDelay);

	return 0;
}

// ---------------------------------------------------------------------------
// 0x7CD8EF - process teardown. Flushes and closes the log.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x7CD8EF, HAres_ExeTerminate, 0x9)
{
	HAres::ExeTerminate();
	return 0;
}

// ===========================================================================
//  TEMPLATE - copy this when adding a hook.
//
//  DEFINE_HOOK(0xADDRESS, ClassName_Method_Purpose, 0xSIZE)
//  {
//      enum { Continue = 0xADDRESS + 0xSIZE, Skip = 0xOTHER };
//
//      GET(TechnoClass*, pThis, ESI);          // object pointer from a register
//      GET_STACK(int, damage, 0x4);            // argument from the stack
//      // LEA_STACK(CoordStruct*, pCoord, 0xC); // pointer to a stack argument
//
//      if (!pThis || !pThis->IsAlive)
//          return Continue;
//
//      // ... your logic ...
//
//      return 0;                               // run the original instructions
//  }
// ===========================================================================
