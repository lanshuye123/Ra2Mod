#pragma once

class TechnoClass;

// ---------------------------------------------------------------------------
//  Feature: change a unit's type when it is promoted.
//
//  Ares 3.0 already ships the base mechanism with two TechnoType keys:
//
//      [MTNK]
//      Promote.VeteranType=MTNK_V      ; become this when reaching one star
//      Promote.EliteType=MTNK_E        ; become this when reaching three stars
//      Promote.VeteranExperience=0.25  ; Ares: veterancy added after converting
//      Promote.VeteranSound= / Promote.VeteranFlash= / EVA.VeteranPromoted=
//
//  HAres reads the same two keys (so a mod that already uses them keeps
//  working) and adds control over the state of the converted unit:
//
//      Promote.KeepHealth=             ; reset to full health, or keep the
//                                      ;   current health percentage (default)
//      Promote.KeepVeterancy=          ; keep the star level (default) or drop
//                                      ;   back to rookie
//
//  Detection: Ares replaces the engine's rank-change block, so the engine-side
//  promotion code (and Phobos' fallback hook at 0x6FA07A) never runs while Ares
//  is loaded. HAres therefore redirects the three Ares call sites that lead into
//  Ares' rank update helper (Ares+0x46AF0) to a wrapper which compares the rank
//  before and after Ares' own handling - the "wrap an Ares internal call site"
//  route described in docs/DEVELOPMENT.md section 5.10.
//
//  Requires Ares 3.0: the conversion itself is Ares' ConvertTypeTo.
// ---------------------------------------------------------------------------
namespace PromoteConvert
{
	// Reads Promote.* for every techno type. Called when a match starts; the
	// hooks also call it lazily so that a loaded savegame works.
	void BuildRegistry();

	// Writes one log line per configured type.
	void LogRegistry();

	// Installs the Ares call-site patches. Must run once Ares is loaded, i.e.
	// from HAres::ExeRun.
	void InitAresIntegration();
}
