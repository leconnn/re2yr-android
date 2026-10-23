#pragma once

// The rules data combat reads: weapons, warheads, projectiles and the rules-wide constants (U sections
// 3 to 7). Everything is read from the rules with the original's defaults and cached by name; the
// indices are internal to one game and never stored in saves or CRCs.

#include "rules/rules_db.h"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra::sim {

// ArmorType order (the Verses list is indexed by it).
enum Armor : int { ArmorNone, ArmorFlak, ArmorPlate, ArmorLight, ArmorMedium, ArmorHeavy, ArmorWood, ArmorSteel, ArmorConcrete, ArmorSpecial1, ArmorSpecial2, kArmorCount };

int armor_from_name(std::string_view name);

struct WarheadInfo {
	std::string name;
	std::array<double, kArmorCount> verses {};
	double cell_spread = 0.0;
	double percent_at_max = 1.0;
	int inf_death = 0;
	bool wall = false, wood = false, tiberium = false, conventional = false, rocker = false, sparky = false, bright = false;
	bool affects_allies = true;
	bool em_effect = false;
	bool wall_absolute_destroyer = false;
	bool psychedelic = false;
	int min_debris = 0, max_debris = 0;
	int shake_x_lo = 0, shake_x_hi = 0, shake_y_lo = 0, shake_y_hi = 0;
};

struct ProjectileInfo {
	std::string name;
	bool arcing = false, aa = false, ag = true, inviso = false, inaccurate = false, floater = false, vertical = false;
	bool level = false, ranged = false, airburst = false, very_high = false, flak_scatter = false;
	bool subject_to_cliffs = false, subject_to_walls = false, subject_to_elevation = false, degenerates = false;
	bool dropping = false;
	int rot = 0, acceleration = 3, arm = 0, cluster = 1, detonation_altitude = 0;
	double elasticity = 0.75;
};

struct WeaponInfo {
	std::string name;
	int damage = 0;
	int rof = 0;
	int range = 0;         // leptons
	int minimum_range = 0; // leptons
	int speed = 100;
	int burst = 1;
	int projectile = -1;
	int warhead = -1;
	bool suicide = false, area_fire = false, lobber = false, never_use = false, cell_rangefinding = false;
	bool decloak_to_fire = false, fire_once = false;
	bool fire_while_moving = true, omni_fire = false;
	bool valid() const { return projectile >= 0 && warhead >= 0; }
};

// Rules-wide constants combat uses.
struct CombatRules {
	int max_damage = 1000;
	int gravity = 3;
	double condition_yellow = 0.5;
	double condition_red = 0.5;
	int ballistic_scatter = 256;
	double missile_rot_var = 0.25;
	int missile_safety_altitude = 500;
	double veteran_armor = 1.0, veteran_combat = 1.0, veteran_rof = 1.0, veteran_speed = 1.0;
	double veteran_ratio = 10.0, veteran_cap = 1.0;
	int bridge_height = 4 * 12; // not read yet
};

class CombatData {
public:
	explicit CombatData(const ra::rules::RulesDb& rules);

	const CombatRules& rules() const { return rules_; }
	CombatRules& rules_for_test() { return rules_; }

	// Indices by name (-1 for an empty or unknown name). Weapons without a projectile or warhead still
	// get an index; WeaponInfo::valid() tells.
	int weapon(std::string_view name);
	int warhead(std::string_view name);
	int projectile(std::string_view name);

	const WeaponInfo& weapon_info(int i) const { return weapons_[size_t(i)]; }
	const WarheadInfo& warhead_info(int i) const { return warheads_[size_t(i)]; }
	const ProjectileInfo& projectile_info(int i) const { return projectiles_[size_t(i)]; }

private:
	const ra::rules::RulesDb* db_;
	CombatRules rules_;
	std::vector<WeaponInfo> weapons_;
	std::vector<WarheadInfo> warheads_;
	std::vector<ProjectileInfo> projectiles_;
	std::unordered_map<std::string, int> weapon_index_, warhead_index_, projectile_index_;
};

} // namespace ra::sim
