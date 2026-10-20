#pragma once

// What the simulation needs to know about an object's type, read once from the rules.
//
// Values come from the type's rules section with the original's defaults (rules/key_table). A few
// movement keys (ROT, Locomotor, SpeedType, MovementZone, Crusher) are not in the key table, so they
// are read as text here with explicit defaults.

#include "rules/rules_db.h"
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

	static TypeInfo read(const ra::rules::RulesDb& rules, ra::rules::TypeKind kind, int index);
};

} // namespace ra::sim
