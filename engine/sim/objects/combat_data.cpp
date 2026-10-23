#include "sim/objects/combat_data.h"

#include "ini/ini.h"
#include "rules/rule_object.h"

namespace ra::sim {

namespace {

using ra::rules::RuleObject;

bool bool_or(const RuleObject& o, const char* key, bool fallback) { return o.has(key) ? o.get_bool(key) : fallback; }
int int_or(const RuleObject& o, const char* key, int fallback) { return o.has(key) ? o.get_int(key) : fallback; }

} // namespace

int armor_from_name(std::string_view name)
{
	static const char* const names[kArmorCount] = { "none", "flak", "plate", "light", "medium", "heavy", "wood", "steel", "concrete", "special_1", "special_2" };
	const std::string n = ra::ini::to_lower(std::string(name));
	for (int i = 0; i < kArmorCount; ++i)
		if (n == names[i])
			return i;
	return ArmorNone;
}

CombatData::CombatData(const ra::rules::RulesDb& rules) : db_(&rules)
{
	const RuleObject g = rules.section("General");
	rules_.max_damage = int_or(g, "MaxDamage", 1000);
	rules_.gravity = int_or(g, "Gravity", 3);
	rules_.condition_yellow = g.has("ConditionYellow") ? g.get_double("ConditionYellow") : 0.5;
	rules_.condition_red = g.has("ConditionRed") ? g.get_double("ConditionRed") : 0.5;
	rules_.ballistic_scatter = g.has("BallisticScatter") ? g.get_leptons("BallisticScatter") : 256;
	rules_.missile_rot_var = g.has("MissileROTVar") ? g.get_double("MissileROTVar") : 0.25;
	rules_.missile_safety_altitude = int_or(g, "MissileSafetyAltitude", 500);
	rules_.veteran_armor = g.has("VeteranArmor") ? g.get_double("VeteranArmor") : 1.0;
	rules_.veteran_combat = g.has("VeteranCombat") ? g.get_double("VeteranCombat") : 1.0;
	rules_.veteran_rof = g.has("VeteranROF") ? g.get_double("VeteranROF") : 1.0;
	rules_.veteran_speed = g.has("VeteranSpeed") ? g.get_double("VeteranSpeed") : 1.0;
	rules_.veteran_ratio = g.has("VeteranRatio") ? g.get_double("VeteranRatio") : 10.0;
	rules_.veteran_cap = g.has("VeteranCap") ? g.get_double("VeteranCap") : 1.0;
}

int CombatData::warhead(std::string_view name)
{
	if (name.empty())
		return -1;
	const std::string key = ra::ini::to_lower(std::string(name));
	if (auto it = warhead_index_.find(key); it != warhead_index_.end())
		return it->second;
	const RuleObject o = db_->section(name, "WarheadTypeClass");
	if (!o.exists())
		return warhead_index_[key] = -1;
	WarheadInfo w;
	w.name = std::string(name);
	w.verses.fill(1.0);
	const std::vector<std::string> list = o.get_list("Verses");
	for (size_t i = 0; i < list.size() && i < w.verses.size(); ++i) {
		double v = 1.0;
		if (ra::rules::read_double(list[i], v))
			w.verses[i] = v;
	}
	w.cell_spread = o.get_double("CellSpread");
	w.percent_at_max = o.has("PercentAtMax") ? o.get_double("PercentAtMax") : 1.0;
	w.inf_death = int_or(o, "InfDeath", 0);
	w.wall = bool_or(o, "Wall", false);
	w.wood = bool_or(o, "Wood", false);
	w.tiberium = bool_or(o, "Tiberium", false);
	w.conventional = bool_or(o, "Conventional", false);
	w.rocker = bool_or(o, "Rocker", false);
	w.sparky = bool_or(o, "Sparky", false);
	w.bright = bool_or(o, "Bright", false);
	w.affects_allies = bool_or(o, "AffectsAllies", true);
	w.em_effect = bool_or(o, "EMEffect", false);
	w.wall_absolute_destroyer = bool_or(o, "WallAbsoluteDestroyer", false);
	w.psychedelic = bool_or(o, "Psychedelic", false);
	w.min_debris = int_or(o, "MinDebris", 0);
	w.max_debris = int_or(o, "MaxDebris", 0);
	w.shake_x_lo = int_or(o, "ShakeXlo", 0);
	w.shake_x_hi = int_or(o, "ShakeXhi", 0);
	w.shake_y_lo = int_or(o, "ShakeYlo", 0);
	w.shake_y_hi = int_or(o, "ShakeYhi", 0);
	warheads_.push_back(std::move(w));
	return warhead_index_[key] = static_cast<int>(warheads_.size()) - 1;
}

int CombatData::projectile(std::string_view name)
{
	if (name.empty())
		return -1;
	const std::string key = ra::ini::to_lower(std::string(name));
	if (auto it = projectile_index_.find(key); it != projectile_index_.end())
		return it->second;
	const RuleObject o = db_->projectile(name);
	if (!o.exists())
		return projectile_index_[key] = -1;
	ProjectileInfo p;
	p.name = std::string(name);
	p.arcing = bool_or(o, "Arcing", false);
	p.aa = bool_or(o, "AA", false);
	p.ag = bool_or(o, "AG", true);
	p.inviso = bool_or(o, "Inviso", false);
	p.inaccurate = bool_or(o, "Inaccurate", false);
	p.floater = bool_or(o, "Floater", false);
	p.vertical = bool_or(o, "Vertical", false);
	p.level = bool_or(o, "Level", false);
	p.ranged = bool_or(o, "Ranged", false);
	p.airburst = bool_or(o, "Airburst", false);
	p.very_high = bool_or(o, "VeryHigh", false);
	p.flak_scatter = bool_or(o, "FlakScatter", false);
	p.subject_to_cliffs = bool_or(o, "SubjectToCliffs", false);
	p.subject_to_walls = bool_or(o, "SubjectToWalls", false);
	p.subject_to_elevation = bool_or(o, "SubjectToElevation", false);
	p.degenerates = bool_or(o, "Degenerates", false);
	p.dropping = bool_or(o, "Dropping", false);
	p.rot = int_or(o, "ROT", 0);
	p.acceleration = int_or(o, "Acceleration", 3);
	p.arm = int_or(o, "Arm", 0);
	p.cluster = int_or(o, "Cluster", 1);
	p.detonation_altitude = int_or(o, "DetonationAltitude", 0);
	p.elasticity = o.has("Elasticity") ? o.get_double("Elasticity") : 0.75;
	projectiles_.push_back(std::move(p));
	return projectile_index_[key] = static_cast<int>(projectiles_.size()) - 1;
}

int CombatData::weapon(std::string_view name)
{
	if (name.empty())
		return -1;
	const std::string key = ra::ini::to_lower(std::string(name));
	if (auto it = weapon_index_.find(key); it != weapon_index_.end())
		return it->second;
	const RuleObject o = db_->weapon(name);
	if (!o.exists())
		return weapon_index_[key] = -1;
	WeaponInfo w;
	w.name = std::string(name);
	w.damage = int_or(o, "Damage", 0);
	w.rof = int_or(o, "ROF", 0);
	w.range = o.has("Range") ? o.get_leptons("Range") : 0;
	w.minimum_range = o.has("MinimumRange") ? o.get_leptons("MinimumRange") : 0;
	w.speed = int_or(o, "Speed", 100);
	w.burst = std::max(1, int_or(o, "Burst", 1));
	w.projectile = projectile(o.get_string("Projectile"));
	w.warhead = warhead(o.get_string("Warhead"));
	w.suicide = bool_or(o, "Suicide", false);
	w.area_fire = bool_or(o, "AreaFire", false);
	w.lobber = bool_or(o, "Lobber", false);
	w.never_use = bool_or(o, "NeverUse", false);
	w.cell_rangefinding = bool_or(o, "CellRangefinding", false);
	w.decloak_to_fire = bool_or(o, "DecloakToFire", false);
	w.fire_once = bool_or(o, "FireOnce", false);
	w.fire_while_moving = bool_or(o, "FireWhileMoving", true);
	w.omni_fire = bool_or(o, "OmniFire", false);
	weapons_.push_back(std::move(w));
	return weapon_index_[key] = static_cast<int>(weapons_.size()) - 1;
}

} // namespace ra::sim
