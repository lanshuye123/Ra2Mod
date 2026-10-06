#pragma once

class TechnoClass;
class WeaponTypeClass;

// ---------------------------------------------------------------------------
//  Feature: mind control shield zones.
//
//  Any techno type (a building or a vehicle make the most sense, but infantry
//  and aircraft work too) may declare a psychic suppression field:
//
//      [GAPSYCH]
//      MindControlShield.Range=8                  ; cells
//      MindControlShield.Houses=all               ; whose units it covers
//      MindControlShield.ReleaseMindControl=yes   ; free controlled units inside
//      MindControlShield.BlockAttack=yes          ; mind control weapons cannot
//                                                 ;   fire from inside
//
//  Inside the radius of a live, on-map provider:
//
//    * a unit that is already mind controlled has its control released (the
//      engine's own CaptureManagerClass::FreeUnit restores the original owner,
//      plays the "mind cleared" sound and drops the control node);
//    * a unit whose selected weapon carries a MindControl warhead cannot fire.
//      The fire error returned is FireError::REARM (the same one Ares and
//      Phobos use for their "weapons disabled" states), so the unit keeps its
//      target and resumes firing once it leaves the field.
//
//  Implementation notes
//  --------------------
//  * The attack block hooks TechnoClass::GetFireError (0x6FC0B0) at 0x6FC356,
//    where EDI already holds the WeaponType the engine picked for this shot.
//    That single point covers every TechnoClass subclass and both the manual
//    and the AI attack paths (the engine calls this virtual, vtable+0x3C0,
//    from 20 sites). Ares and Phobos do not use this address.
//  * The per-frame release pass runs from TechnoClass::AI (0x6F9E50); Ares and
//    Phobos both hook it and both return 0, so returning 0 as well keeps the
//    Syringe chain intact.
// ---------------------------------------------------------------------------
namespace MindControlShield
{
	// Reads MindControlShield.* for every techno type. Called when a match
	// starts; the hooks also call it lazily so that a loaded savegame works.
	void BuildRegistry();

	// Writes one log line per configured type.
	void LogRegistry();

	// Once per logic frame: refreshes the list of active shield zones and
	// releases mind control inside them.
	void Update();

	// True when pAttacker may not fire pWeapon because it stands inside a mind
	// control shield.
	bool BlocksFiring(TechnoClass* pAttacker, WeaponTypeClass* pWeapon);
}
