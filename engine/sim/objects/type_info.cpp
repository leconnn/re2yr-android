#include "sim/objects/type_info.h"

#include "ini/ini.h"

#include <cctype>
#include <cstdio>

namespace ra::sim {

namespace {

using ra::rules::RuleObject;

int int_or(const RuleObject& o, const char* key, int fallback)
{
	return o.has(key) ? o.get_int(key) : fallback;
}
bool bool_or(const RuleObject& o, const char* key, bool fallback)
{
	return o.has(key) ? o.get_bool(key) : fallback;
}
double double_or(const RuleObject& o, const char* key, double fallback)
{
	return o.has(key) ? o.get_double(key) : fallback;
}

} // namespace

// The MovementZone names, in enum order (Subterrannean is spelt both ways in the rules files).
int movement_zone_from_name(std::string_view name)
{
	static const char* const names[] = { "normal", "crusher", "destroyer", "amphibiousdestroyer", "amphibiouscrusher", "amphibious",
		"subterrannean", "infantry", "infantrydestroyer", "fly", "water", "waterbeach", "crusherall" };
	const std::string n = ra::ini::to_lower(std::string(name));
	if (n == "subterannean")
		return 6;
	for (int i = 0; i < 13; ++i)
		if (n == names[i])
			return i;
	return 0;
}

SpeedType speed_type_from_name(std::string_view name)
{
	const std::string n = ra::ini::to_lower(name);
	if (n == "foot") return SpeedType::Foot;
	if (n == "track") return SpeedType::Track;
	if (n == "wheel") return SpeedType::Wheel;
	if (n == "hover") return SpeedType::Hover;
	if (n == "winged") return SpeedType::Winged;
	if (n == "float") return SpeedType::Float;
	if (n == "amphibious") return SpeedType::Amphibious;
	if (n == "floatbeach") return SpeedType::FloatBeach;
	return SpeedType::None;
}

LocomotorKind locomotor_from_clsid(std::string_view text)
{
	// {4A582741-9839-11d1-B709-00A024DDAFD1} and the other ".-9839-11D1-B709-00A024DDAFD1" ids, by their first group.
	const std::string t = ra::ini::to_lower(text);
	struct Entry {
		const char* id;
		LocomotorKind kind;
	};
	static const Entry entries[] = {
		{ "4a582741", LocomotorKind::Drive }, { "4a582742", LocomotorKind::Hover }, { "4a582743", LocomotorKind::Tunnel },
		{ "4a582744", LocomotorKind::Walk },  { "4a582745", LocomotorKind::DropPod }, { "4a582746", LocomotorKind::Fly },
		{ "4a582747", LocomotorKind::Teleport }, { "4a582748", LocomotorKind::Mech }, { "4a582749", LocomotorKind::Ship },
		{ "92612c46", LocomotorKind::Jumpjet }, { "b7b49766", LocomotorKind::Rocket },
	};
	for (const Entry& e : entries)
		if (t.find(e.id) != std::string::npos)
			return e.kind;
	if (t == "drive") return LocomotorKind::Drive;
	if (t == "walk") return LocomotorKind::Walk;
	if (t == "fly") return LocomotorKind::Fly;
	if (t == "ship") return LocomotorKind::Ship;
	return LocomotorKind::None;
}

void LandSpeeds::load(const ra::rules::RulesDb& rules)
{
	static const char* const lands[kLands] = { "Clear", "Road", "Water", "Rock", "Wall", "Tiberium", "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds" };
	static const char* const speeds[kSpeedTypeCount] = { "Foot", "Track", "Wheel", "Hover", "Winged", "Float", "Amphibious", "FloatBeach" };
	for (int l = 0; l < kLands; ++l) {
		const RuleObject section = rules.section(lands[l], "LandTypeClass");
		for (int s = 0; s < kSpeedTypeCount; ++s) {
			double v = 0.0;
			if (section.has(speeds[s]))
				v = section.get_double(speeds[s]);
			else if (s == static_cast<int>(SpeedType::Winged))
				v = 1.0;
			table_[size_t(l)][size_t(s)] = v;
		}
	}
}

double LandSpeeds::multiplier(ra::world::LandType land, SpeedType speed) const
{
	const int l = static_cast<int>(land), s = static_cast<int>(speed);
	if (l < 0 || l >= kLands || s < 0 || s >= kSpeedTypeCount)
		return 0.0;
	return table_[size_t(l)][size_t(s)];
}

TypeInfo TypeInfo::read(const ra::rules::RulesDb& rules, ra::rules::TypeKind kind, int index)
{
	TypeInfo t;
	t.kind = kind;
	t.index = index;
	t.name = std::string(rules.type_name(kind, index));
	const RuleObject o = rules.object(kind, index);
	t.strength = int_or(o, "Strength", 1);
	t.speed = int_or(o, "Speed", 0);
	t.rot = int_or(o, "ROT", 0);
	t.sight = int_or(o, "Sight", 0);
	t.size = double_or(o, "Size", 1.0);
	t.crusher = bool_or(o, "Crusher", false);
	t.crushable = bool_or(o, "Crushable", kind == ra::rules::TypeKind::Infantry);
	t.turret = bool_or(o, "Turret", false);
	t.accelerates = bool_or(o, "Accelerates", true);
	t.acceleration_factor = double_or(o, "AccelerationFactor", 0.03);
	t.deceleration_factor = double_or(o, "DecelerationFactor", 0.002);
	t.slowdown_distance = int_or(o, "SlowdownDistance", 500);
	t.guard_range = o.get_leptons("GuardRange");
	t.movement_zone = o.has("MovementZone") ? movement_zone_from_name(o.get_string("MovementZone")) : 0;
	t.train = bool_or(o, "IsTrain", false);
	{
		const RuleObject jj = rules.section("JumpjetControls");
		const auto jint = [&](const char* type_key, const char* key, int fallback) {
			return o.has(type_key) ? o.get_int(type_key) : (jj.has(key) ? jj.get_int(key) : fallback);
		};
		const auto jdouble = [&](const char* type_key, const char* key, double fallback) {
			return o.has(type_key) ? o.get_double(type_key) : (jj.has(key) ? jj.get_double(key) : fallback);
		};
		t.jj_turn_rate = jint("JumpjetTurnRate", "TurnRate", 4);
		t.jj_speed = jint("JumpjetSpeed", "Speed", 14);
		t.jj_climb = jint("JumpjetClimb", "Climb", 5);
		t.jj_crash = jdouble("JumpjetCrash", "Crash", 5.0);
		t.jj_height = jint("JumpjetHeight", "Height", 500);
		t.jj_accel = jint("JumpjetAccel", "Accel", 2);
		t.jj_wobbles = jdouble("JumpjetWobbles", "Wobbles", 0.15);
		t.jj_deviation = jint("JumpjetDeviation", "Deviation", 2);
		t.jj_no_wobbles = o.has("JumpjetNoWobbles") ? o.get_bool("JumpjetNoWobbles") : (jj.has("NoWobbles") && jj.get_bool("NoWobbles"));
	}
	t.balloon_hover = bool_or(o, "BalloonHover", false);
	t.flight_level = int_or(o, "FlightLevel", -1);
	t.landable = bool_or(o, "Landable", false);
	t.omni_crusher = bool_or(o, "OmniCrusher", false);
	t.armor = armor_from_name(o.get_string("Armor"));
	t.primary_name = o.get_string("Primary");
	t.secondary_name = o.get_string("Secondary");
	t.ammo = int_or(o, "Ammo", -1);
	t.explodes = bool_or(o, "Explodes", false);
	t.type_immune = bool_or(o, "TypeImmune", false);
	t.natural = bool_or(o, "Natural", false);
	t.unnatural = bool_or(o, "Unnatural", false);
	t.can_c4 = bool_or(o, "CanC4", true);
	t.no_auto_fire = bool_or(o, "NoAutoFire", false);
	t.air_range_bonus = o.has("AirRangeBonus") ? o.get_leptons("AirRangeBonus") : 0;
	t.damage_self = bool_or(o, "DamageSelf", false);
	t.tech_level = int_or(o, "TechLevel", 255);
	t.build_limit = o.has("BuildLimit") ? o.get_int("BuildLimit") : 0x7FFFFFFF;
	t.build_time_multiplier = double_or(o, "BuildTimeMultiplier", 1.0);
	t.prerequisites = o.get_list("Prerequisite");
	t.owners = o.get_list("Owner");
	t.forbidden_houses = o.get_list("ForbiddenHouses");
	t.prerequisite_override = o.get_list("PrerequisiteOverride");
	t.naval = bool_or(o, "Naval", false);
	t.soylent = int_or(o, "Soylent", 0);
	t.free_unit = o.get_string("FreeUnit");
	t.harvester = bool_or(o, "Harvester", false);
	t.weeder = bool_or(o, "Weeder", false);
	t.refinery = bool_or(o, "Refinery", false);
	t.ore_purifier = bool_or(o, "OrePurifier", false);
	t.resource_gatherer = bool_or(o, "ResourceGatherer", false);
	t.resource_destination = bool_or(o, "ResourceDestination", false);
	t.storage = int_or(o, "Storage", 0);
	t.docks = o.get_list("Dock");
	if (kind == ra::rules::TypeKind::Building) {
		const int power = int_or(o, "Power", 0);
		t.power_bonus = power > 0 ? power : 0;
		t.power_drain = power < 0 ? -power : 0;
		t.powered = bool_or(o, "Powered", false);
		t.powered_special = bool_or(o, "PoweredSpecial", false);
		t.base_normal = bool_or(o, "BaseNormal", true);
		t.place_anywhere = bool_or(o, "PlaceAnywhere", false);
		t.factory_plant = bool_or(o, "FactoryPlant", false);
		t.wall = bool_or(o, "Wall", false);
		t.needs_engineer = bool_or(o, "NeedsEngineer", false);
		t.combat_building = ra::ini::to_lower(o.get_string("BuildCat")) == "combat";
		const std::string factory = ra::ini::to_lower(o.get_string("Factory"));
		t.factory_kind = factory == "buildingtype" ? 1 : factory == "infantrytype" ? 2 : factory == "unittype" ? 3 : factory == "aircrafttype" ? 4 : 0;
		t.adjacent = int_or(o, "Adjacent", 3);
		t.cost_bonus[0] = double_or(o, "InfantryCostBonus", 1.0);
		t.cost_bonus[1] = double_or(o, "UnitsCostBonus", 1.0);
		t.cost_bonus[2] = double_or(o, "AircraftCostBonus", 1.0);
		t.cost_bonus[3] = double_or(o, "BuildingsCostBonus", 1.0);
		t.cost_bonus[4] = double_or(o, "DefensesCostBonus", 1.0);
		t.produce_cash_amount = int_or(o, "ProduceCashAmount", 0);
		t.produce_cash_delay = int_or(o, "ProduceCashDelay", 0);
		t.produce_cash_startup = int_or(o, "ProduceCashStartup", 0);
	}
	t.cost = int_or(o, "Cost", 0);
	t.trainable = bool_or(o, "Trainable", true);
	t.dont_score = bool_or(o, "DontScore", false);
	t.self_healing = bool_or(o, "SelfHealing", false);
	t.organic = bool_or(o, "Organic", false);
	{
		static const char* const abilities[] = { "FASTER", "STRONGER", "FIREPOWER", "SCATTER", "ROF", "SIGHT", "CLOAK", "TIBERIUM_PROOF", "VEIN_PROOF",
			"SELF_HEAL", "EXPLODES", "RADAR_INVISIBLE", "SENSORS", "FEARLESS", "C4", "TIBERIUM_HEAL", "GUARD_AREA", "CRUSHER" };
		const auto mask = [&](const char* key) {
			uint32_t m = 0;
			for (const std::string& item : o.get_list(key))
				for (int i = 0; i < 18; ++i)
					if (ra::ini::to_lower(item) == ra::ini::to_lower(abilities[i]))
						m |= 1u << i;
			return m;
		};
		t.veteran_abilities = mask("VeteranAbilities");
		t.elite_abilities = mask("EliteAbilities");
	}
	t.min_debris = int_or(o, "MinDebris", 0);
	t.max_debris = int_or(o, "MaxDebris", 0);
	t.can_approach_target = bool_or(o, "CanApproachTarget", true);
	t.can_recalc_approach_target = bool_or(o, "CanRecalcApproachTarget", true);
	t.close_range = bool_or(o, "CloseRange", false);
	t.can_passive_acquire = bool_or(o, "CanPassiveAquire", true);
	t.can_retaliate = bool_or(o, "CanRetaliate", true);
	t.opportunity_fire = bool_or(o, "OpportunityFire", false);
	t.distributed_fire = bool_or(o, "DistributedFire", false);
	t.mobile_fire = bool_or(o, "MobileFire", true);
	t.turret_spins = bool_or(o, "TurretSpins", false);
	t.hunter_seeker = bool_or(o, "HunterSeeker", false);
	t.vehicle_thief = bool_or(o, "VehicleThief", false);
	t.fraidycat = bool_or(o, "Fraidycat", false);
	t.auto_crush = bool_or(o, "AutoCrush", false);
	t.land_targeting = int_or(o, "LandTargeting", 0);
	t.special_threat_value = int_or(o, "SpecialThreatValue", 0);
	t.fire_up = int_or(o, "FireUp", 0);
	t.fire_prone = int_or(o, "FireProne", 0);
	t.secondary_fire = int_or(o, "SecondaryFire", 0);
	t.secondary_prone = int_or(o, "SecondaryProne", 0);
	if (kind == ra::rules::TypeKind::Infantry) {
		// The sequence section named by the art's Sequence= key: "Name=start,count,facing step".
		static const char* const names[42] = { "Ready", "Guard", "Prone", "Walk", "FireUp", "Down", "Crawl", "Up", "FireProne", "Idle1", "Idle2",
			"Die1", "Die2", "Die3", "Die4", "Die5", "Tread", "Swim", "WetIdle1", "WetIdle2", "WetDie1", "WetDie2", "WetAttack", "Hover", "Fly",
			"Tumble", "FireFly", "Deploy", "Deployed", "DeployedFire", "DeployedIdle", "Undeploy", "Cheer", "Paradrop", "AirDeathStart",
			"AirDeathFalling", "AirDeathFinish", "Panic", "Shovel", "Carry", "SecondaryFire", "SecondaryProne" };
		const std::string seq = rules.art().get_string(rules.image_name(o), "Sequence");
		if (!seq.empty())
			for (int i = 0; i < 42; ++i) {
				const std::string v = rules.art().get_string(seq, names[i]);
				int start = 0, count = 0;
				if (!v.empty() && std::sscanf(v.c_str(), "%d,%d", &start, &count) == 2)
					t.sequence_count[size_t(i)] = count;
			}
	}
	if (kind == ra::rules::TypeKind::Building) {
		const std::string foundation = rules.art().get_string(rules.image_name(o), "Foundation");
		if (!foundation.empty())
			std::sscanf(foundation.c_str(), "%dx%d", &t.foundation_w, &t.foundation_h);
	}

	// Infantry walk, whatever the INI says; vehicles name their locomotor, and default to Drive.
	if (kind == ra::rules::TypeKind::Infantry) {
		t.locomotor = LocomotorKind::Walk;
		if (o.has("Locomotor") && locomotor_from_clsid(o.get_string("Locomotor")) == LocomotorKind::Jumpjet)
			t.locomotor = LocomotorKind::Jumpjet;
		t.speed_type = o.has("SpeedType") ? speed_type_from_name(o.get_string("SpeedType")) : SpeedType::Foot;
	} else {
		const bool aircraft = kind == ra::rules::TypeKind::Aircraft;
		t.locomotor = o.has("Locomotor") ? locomotor_from_clsid(o.get_string("Locomotor")) : (aircraft ? LocomotorKind::Fly : LocomotorKind::Drive);
		t.speed_type = o.has("SpeedType") ? speed_type_from_name(o.get_string("SpeedType")) : (aircraft ? SpeedType::Winged : SpeedType::Track);
	}
	return t;
}

} // namespace ra::sim
