#include "sim/objects/type_info.h"

#include "ini/ini.h"

#include <cctype>

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
	t.crushable = bool_or(o, "Crushable", false);
	t.turret = bool_or(o, "Turret", false);
	t.accelerates = bool_or(o, "Accelerates", true);
	t.acceleration_factor = double_or(o, "AccelerationFactor", 0.03);
	t.deceleration_factor = double_or(o, "DecelerationFactor", 0.002);
	t.slowdown_distance = int_or(o, "SlowdownDistance", 500);
	t.guard_range = o.get_leptons("GuardRange");

	// Infantry walk, whatever the INI says; vehicles name their locomotor, and default to Drive.
	if (kind == ra::rules::TypeKind::Infantry) {
		t.locomotor = LocomotorKind::Walk;
		t.speed_type = o.has("SpeedType") ? speed_type_from_name(o.get_string("SpeedType")) : SpeedType::Foot;
	} else {
		t.locomotor = o.has("Locomotor") ? locomotor_from_clsid(o.get_string("Locomotor")) : LocomotorKind::Drive;
		t.speed_type = o.has("SpeedType") ? speed_type_from_name(o.get_string("SpeedType")) : SpeedType::Track;
	}
	return t;
}

} // namespace ra::sim
