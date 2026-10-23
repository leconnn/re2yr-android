#pragma once

// What the simulation needs to know about an object's type, read once from the rules.
//
// Values come from the type's rules section with the original's defaults (rules/key_table). A few
// movement keys (ROT, Locomotor, SpeedType, MovementZone, Crusher) are not in the key table, so they
// are read as text here with explicit defaults.

#include "rules/rules_db.h"
#include "sim/objects/combat_data.h"
#include "sim/world/land.h"

#include <array>
#include <string>

namespace ra::sim {

enum class SpeedType : int8_t { None = -1, Foot = 0, Track, Wheel, Hover, Winged, Float, Amphibious, FloatBeach, Count };
inline constexpr int kSpeedTypeCount = 8;

enum class LocomotorKind : uint8_t { None, Drive, Hover, Tunnel, Walk, DropPod, Fly, Teleport, Mech, Ship, Jumpjet, Rocket };

SpeedType speed_type_from_name(std::string_view name);
LocomotorKind locomotor_from_clsid(std::string_view clsid_or_name);

// The speed multiplier of each land type for each speed type ([Clear] Foot=100% ...), 0 = impassable.
class LandSpeeds {
public:
	void load(const ra::rules::RulesDb& rules);
	double multiplier(ra::world::LandType land, SpeedType speed) const;

private:
	static constexpr int kLands = 12;
	std::array<std::array<double, kSpeedTypeCount>, kLands> table_ {};
};

struct TypeInfo {
	ra::rules::TypeKind kind = ra::rules::TypeKind::Vehicle;
	int index = -1;
	std::string name;

	int strength = 1;
	int speed = 0;                 // Speed=, leptons per frame before multipliers
	int rot = 0;                   // ROT= (rate of turn)
	int sight = 0;                 // Sight=, in cells
	double size = 1.0;
	SpeedType speed_type = SpeedType::None;
	LocomotorKind locomotor = LocomotorKind::None;
	bool crusher = false;
	bool crushable = false;
	bool turret = false;
	bool accelerates = true;
	double acceleration_factor = 0.03;
	double deceleration_factor = 0.002;
	int slowdown_distance = 500;
	int guard_range = 0;           // leptons
	int movement_zone = 0;         // MovementZone= (0 Normal ... 12 CrusherAll)
	bool train = false;            // IsTrain=
	// Jumpjet ([JumpjetControls] values, overridden by Jumpjet<Name>= in the type).
	int jj_turn_rate = 4;
	int jj_speed = 14;
	int jj_climb = 5;
	double jj_crash = 5.0;
	int jj_height = 500;
	int jj_accel = 2;
	double jj_wobbles = 0.15;
	bool jj_no_wobbles = false;
	int jj_deviation = 2;
	bool balloon_hover = false;
	int flight_level = -1;         // FlightLevel= (-1: Rules FlightLevel)
	bool landable = false;
	bool omni_crusher = false;
	// Combat.
	int armor = 0;                 // Armor= (ArmorType index)
	std::string primary_name, secondary_name;
	int weapon[2] = { -1, -1 };    // CombatData weapon indices of Primary / Secondary, resolved when the type is first used
	int ammo = -1;
	bool explodes = false;
	bool type_immune = false;
	bool natural = false, unnatural = false;
	bool can_c4 = true;
	bool no_auto_fire = false;
	bool turret_recoil = false;
	int air_range_bonus = 0;
	bool damage_self = false;
	// Economy and production (P3 section 9).
	int tech_level = 255;
	int build_limit = 0x7FFFFFFF;
	double build_time_multiplier = 1.0;
	std::vector<std::string> prerequisites, owners, forbidden_houses, prerequisite_override;
	bool naval = false;
	// Buildings.
	int power_bonus = 0, power_drain = 0;
	bool powered = false, powered_special = false, base_normal = true, place_anywhere = false, factory_plant = false;
	bool wall = false, needs_engineer = false;
	bool combat_building = false;       // BuildCat=Combat: counts as a defense
	int factory_kind = 0;               // Factory=: 0 none, 1 BuildingType, 2 InfantryType, 3 UnitType, 4 AircraftType
	int adjacent = 3;
	double cost_bonus[5] = { 1, 1, 1, 1, 1 }; // FactoryPlant: Infantry, Units, Aircraft, Buildings, Defenses
	int produce_cash_amount = 0, produce_cash_delay = 0, produce_cash_startup = 0;
	std::string free_unit;
	int soylent = 0;
	// Harvesting.
	bool harvester = false, weeder = false, refinery = false, ore_purifier = false, resource_gatherer = false, resource_destination = false;
	int storage = 0;                    // bales a harvester holds
	std::vector<std::string> docks;     // Dock=: the buildings it unloads at
	// Veterancy and healing (U section 12).
	int cost = 0;
	bool trainable = true, dont_score = false, self_healing = false, organic = false;
	uint32_t veteran_abilities = 0, elite_abilities = 0; // bit i = ability i
	int min_debris = 0, max_debris = 0;
	int foundation_w = 1, foundation_h = 1; // buildings
	// The firing AI (U section 16).
	bool can_approach_target = true, can_recalc_approach_target = true, close_range = false;
	bool can_passive_acquire = true, can_retaliate = true, opportunity_fire = false, distributed_fire = false;
	bool mobile_fire = true, turret_spins = false, hunter_seeker = false, vehicle_thief = false, fraidycat = false;
	bool auto_crush = false;
	int land_targeting = 0;
	int special_threat_value = 0;
	int fire_up = 0, fire_prone = 0, secondary_fire = 0, secondary_prone = 0; // infantry fire frames
	std::array<int, 42> sequence_count {}; // infantry: frames in each sequence (art [..Sequence])

	static TypeInfo read(const ra::rules::RulesDb& rules, ra::rules::TypeKind kind, int index);
};

} // namespace ra::sim
