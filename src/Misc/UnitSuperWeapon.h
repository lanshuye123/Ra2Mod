#pragma once
#include <cstddef>
#include <vector>

class HouseClass;
class TechnoTypeClass;

// ---------------------------------------------------------------------------
//  Unit-provided superweapons.
//
//  Vanilla (and Ares) only let *buildings* provide superweapons to a house: the
//  house scans its BuildingClass list and, for each BuildingTypeClass whose
//  SuperWeapon / SuperWeapon2 index is set, marks the matching superweapon as
//  available. Ares re-implemented that scan and added per-type lists and upgrade
//  support.
//
//  This feature lets vehicle, infantry and aircraft types do the same job, with
//  the same INI keys a building type uses:
//
//      [MTNK]                  ; any VehicleTypes / InfantryTypes / AircraftTypes entry
//      SuperWeapon=IronCurtainSpecial
//      SuperWeapon2=AmericasParaDropSpecial
//      SuperWeapons=ChronoSphereSpecial,AmericasParaDropSpecial   ; list form
//
//  While the house owns at least one such unit the superweapon is granted; when
//  the last one is destroyed or deploys away, the superweapon is lost again. The
//  weapon is still fired the normal way - sidebar button, then a target on the
//  map - so the unit behaves like a mobile provider, not like a weapon of its own.
//
//  Requires Ares: Ares replaces the engine's whole availability scan, so there is
//  no engine code left to hook. See AresUnitSuperWeapon.cpp.
// ---------------------------------------------------------------------------
namespace UnitSuperWeapon
{
	struct Provider
	{
		TechnoTypeClass* Type;
		int SWIndex;        // index into SuperWeaponTypeClass::Array
	};

	// Re-reads the provider table from rulesmd.ini. Cheap; call once the rules are
	// loaded (HAres does this from ScenarioClass::Start).
	void BuildRegistry();

	// Distinct superweapon indices that at least one unit type can provide.
	const std::vector<int>& GetUnitProvidedIndices();

	// Does pHouse currently own at least one deployed provider unit for this
	// superweapon? Uses HouseClass' per-type "active" counters, so it is O(1) per
	// registered provider rather than a scan over all objects.
	bool IsProvidedByHouse(HouseClass* pHouse, int swIndex);

	// Installs the Ares.dll patch that folds unit providers into Ares'
	// superweapon availability computation. Must run after Ares is loaded; safely
	// does nothing when Ares is absent or its version is unknown.
	void InitAresIntegration();

	// Diagnostics for HAres.log.
	void LogRegistry();
	void TickDiagnostics();
}
