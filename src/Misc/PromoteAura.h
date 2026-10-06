#pragma once

// CellStruct is a type alias (Vector2D<short>), so it cannot be forward
// declared - this header needs the real game declarations.
#include <YRPP.h>

class SuperClass;
class SuperWeaponTypeClass;

// ---------------------------------------------------------------------------
//  Feature: the "PromoteAura" superweapon type.
//
//  A new superweapon effect, selected in rulesmd.ini with
//
//      [PromoteAuraSpecial]
//      Type=PromoteAura        ; <- HAres recognises this name
//      Action=Custom           ; <- Ares turns this into "this superweapon may
//                              ;    be fired at a target", so the cursor works
//
//  and configured with the PromoteAura.* keys documented in
//  docs/functions/promote-aura-superweapon.md. When fired it changes the
//  veterancy of every techno inside PromoteAura.Range cells of the target
//  cell: positive PromoteAura.Levels promotes (rookie -> veteran -> elite),
//  negative demotes.
//
//  Implementation notes
//  --------------------
//  The hook sits on SuperClass::Launch (0x6CC390), which every firing path
//  goes through. Ares hooks it too, but returns 0 for superweapons it does not
//  know, so the chain reaches HAres. Because the engine has no idea what
//  "Type=PromoteAura" means, its own dispatch table would either do nothing or
//  (worse) run an unrelated vanilla effect, so the registry forces the type's
//  SuperWeaponType to Invalid while the match is running. Ares guards that case
//  (SWTypeExt::ExtData::GetNewSWType only indexes its array for real custom
//  types), so the superweapon still charges, shows its cameo and fires like any
//  other.
//
//  Firing also drops the "place superweapon" selection (Unsorted::CurrentSWType,
//  0x8809A0). The engine only clears it inside each vanilla superweapon's own
//  case handler, and Ares only for its own NewSWType classes, so a type neither
//  of them knows would otherwise leave the superweapon cursor stuck on screen
//  after the shot.
// ---------------------------------------------------------------------------
namespace PromoteAura
{
	// The rulesmd.ini Type= name that marks a superweapon as this new type.
	extern const char* const TypeName;

	// Reads PromoteAura.* for every superweapon type. Called when a match
	// starts; the hooks also call it lazily so that a loaded savegame works.
	void BuildRegistry();

	// Writes one log line per configured superweapon.
	void LogRegistry();

	// True when this superweapon type is a PromoteAura superweapon.
	bool IsPromoteAura(const SuperWeaponTypeClass* pSWType);

	// Applies the aura of pSuper at the target cell. Does nothing for
	// superweapons that are not PromoteAura.
	void Apply(SuperClass* pSuper, const CellStruct& cell);
}
