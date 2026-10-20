#include "rules/rules_db.h"

#include <algorithm>
#include <unordered_set>

namespace ra::rules {

namespace {

struct KindInfo {
	TypeKind kind;
	const char* list;
	const char* owner;
};

constexpr KindInfo kKinds[kTypeKindCount] = {
	{ TypeKind::Country, "Countries", "HouseTypeClass" },
	{ TypeKind::Infantry, "InfantryTypes", "InfantryTypeClass" },
	{ TypeKind::Vehicle, "VehicleTypes", "UnitTypeClass" },
	{ TypeKind::Aircraft, "AircraftTypes", "AircraftTypeClass" },
	{ TypeKind::Building, "BuildingTypes", "BuildingTypeClass" },
	{ TypeKind::Terrain, "TerrainTypes", "TerrainTypeClass" },
	{ TypeKind::Smudge, "SmudgeTypes", "SmudgeTypeClass" },
	{ TypeKind::Overlay, "OverlayTypes", "OverlayTypeClass" },
	{ TypeKind::Animation, "Animations", "AnimTypeClass" },
	{ TypeKind::VoxelAnim, "VoxelAnims", "VoxelAnimTypeClass" },
	{ TypeKind::Particle, "Particles", "ParticleTypeClass" },
	{ TypeKind::ParticleSystem, "ParticleSystems", "ParticleSystemTypeClass" },
	{ TypeKind::SuperWeapon, "SuperWeaponTypes", "SuperWeaponTypeClass" },
	{ TypeKind::Warhead, "Warheads", "WarheadTypeClass" },
	{ TypeKind::Tiberium, "Tiberiums", "TiberiumClass" },
};

// Sections of a map that hold the map's own data, not rules.
bool is_map_only_section(const std::string& lower_name)
{
	static const std::unordered_set<std::string> names = {
		"basic", "map", "lighting", "preview", "previewpack", "isomappack5", "overlaypack", "overlaydatapack", "waypoints",
		"terrain", "units", "infantry", "structures", "aircraft", "smudge", "celltags", "houses", "triggers", "events",
		"actions", "tags", "taskforces", "scripttypes", "teamtypes", "aitriggertypes", "aitriggertypesenable",
		"variablenames", "specialflags", "ranking", "digest", "header", "tubes", "banner",
	};
	return names.count(lower_name) != 0;
}

} // namespace

const char* list_section_name(TypeKind kind)
{
	return kKinds[static_cast<size_t>(kind)].list;
}

const char* owner_class_name(TypeKind kind)
{
	return kKinds[static_cast<size_t>(kind)].owner;
}

void RulesDb::load_from_text(GameKind kind, const RulesSources& sources)
{
	game_ = kind;
	rules_ = ra::ini::IniFile::parse(sources.rules);
	for (std::string_view extra : sources.rules_overrides)
		rules_.merge(ra::ini::IniFile::parse(extra));
	art_ = ra::ini::IniFile::parse(sources.art);
	ai_ = ra::ini::IniFile::parse(sources.ai);
	sound_ = ra::ini::IniFile::parse(sources.sound);
	eva_ = ra::ini::IniFile::parse(sources.eva);
	theme_ = ra::ini::IniFile::parse(sources.theme);
	rebuild_registry();
}

bool RulesDb::load(const ra::vfs::Vfs& vfs, GameKind kind, std::string* error)
{
	const GameFiles& files = game_files(kind);
	const auto rules = vfs.open(files.rules);
	const auto art = vfs.open(files.art);
	if (!rules || !art) {
		if (error)
			*error = std::string("cannot find ") + (rules ? files.art : files.rules);
		return false;
	}
	// Loose RULE*.INI files next to the game override the built-in values. (RULEMD*.INI for
	// Yuri's Revenge, RULE*.INI for Red Alert 2.) The main file itself is found like any other.
	std::vector<ra::vfs::FileData> override_data;
	const std::string main_name = files.rules;
	for (const std::string& name : vfs.loose_files_matching(kind == GameKind::YurisRevenge ? "rulemd" : "rule", ".ini")) {
		if (ra::ini::to_lower(name) == ra::ini::to_lower(main_name))
			continue;
		if (auto data = vfs.open(name))
			override_data.push_back(std::move(data));
	}

	RulesSources sources;
	sources.rules = rules.text();
	for (const auto& d : override_data)
		sources.rules_overrides.push_back(d.text());
	sources.art = art.text();
	const auto ai = vfs.open(files.ai), sound = vfs.open(files.sound), eva = vfs.open(files.eva), theme = vfs.open(files.theme);
	if (ai)
		sources.ai = ai.text();
	if (sound)
		sources.sound = sound.text();
	if (eva)
		sources.eva = eva.text();
	if (theme)
		sources.theme = theme.text();
	load_from_text(kind, sources);

	if (const auto csf = vfs.open(files.strings)) {
		ra::formats::CsfFile strings;
		if (ra::formats::CsfFile::parse(csf.bytes, strings))
			strings_ = std::move(strings);
	}
	return true;
}

void RulesDb::rebuild_registry()
{
	for (int k = 0; k < kTypeKindCount; ++k) {
		types_[k].clear();
		type_lookup_[k].clear();
	}
	add_type_names_from(rules_, true);
}

// Types are listed by position: the Nth entry of [InfantryTypes] is infantry type N, whatever its key.
void RulesDb::add_type_names_from(const ra::ini::IniFile& ini, bool /*replace*/)
{
	for (const KindInfo& info : kKinds) {
		const ra::ini::IniSection* list = ini.section(info.list);
		if (!list)
			continue;
		auto& names = types_[static_cast<size_t>(info.kind)];
		auto& lookup = type_lookup_[static_cast<size_t>(info.kind)];
		for (const auto& [key, value] : list->entries) {
			const std::string name(ra::ini::trim(value));
			if (name.empty())
				continue;
			const std::string lower = ra::ini::to_lower(name);
			if (lookup.count(lower))
				continue; // the game doesn't register a type twice
			lookup.emplace(lower, static_cast<int>(names.size()));
			names.push_back(name);
		}
	}
}

int RulesDb::type_index(TypeKind kind, std::string_view name) const
{
	const auto& lookup = type_lookup_[static_cast<size_t>(kind)];
	auto it = lookup.find(ra::ini::to_lower(ra::ini::trim(name)));
	return it == lookup.end() ? -1 : it->second;
}

std::string_view RulesDb::type_name(TypeKind kind, int index) const
{
	const auto& names = type_names(kind);
	if (index < 0 || index >= static_cast<int>(names.size()))
		return {};
	return names[static_cast<size_t>(index)];
}

RuleObject RulesDb::object(TypeKind kind, int index) const
{
	return object(kind, type_name(kind, index));
}

RuleObject RulesDb::object(TypeKind kind, std::string_view name) const
{
	return RuleObject(rules_.section(name), owner_class_name(kind));
}

RuleObject RulesDb::section(std::string_view name, std::string_view owner_class) const
{
	return RuleObject(rules_.section(name), std::string(owner_class));
}

RuleObject RulesDb::art_section(std::string_view name, std::string_view owner_class) const
{
	return RuleObject(art_.section(name), std::string(owner_class));
}

std::string RulesDb::image_name(const RuleObject& object) const
{
	std::string image = object.get_string("Image");
	return image.empty() ? std::string(object.name()) : image;
}

void RulesDb::apply_map(const ra::ini::IniFile& map)
{
	std::unordered_set<std::string> list_sections;
	for (const KindInfo& info : kKinds)
		list_sections.insert(ra::ini::to_lower(info.list));

	// Names of the houses the map defines: their sections hold the house's setup, not rules.
	std::unordered_set<std::string> house_sections;
	if (const auto* houses = map.section("Houses")) {
		for (const auto& [key, value] : houses->entries)
			house_sections.insert(ra::ini::to_lower(ra::ini::trim(value)));
	}

	for (const ra::ini::IniSection& s : map.sections()) {
		const std::string lower = ra::ini::to_lower(s.name);
		if (is_map_only_section(lower) || list_sections.count(lower) || house_sections.count(lower))
			continue;
		ra::ini::IniSection& dest = rules_.section_mut(s.name);
		for (const auto& [key, value] : s.entries)
			dest.set(key, value);
	}
	// New types the map lists are registered after the existing ones.
	add_type_names_from(map, false);
}

} // namespace ra::rules
