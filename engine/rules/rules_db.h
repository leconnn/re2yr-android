#pragma once

// The rules database: the game's data files read into one place.
//
// Loading follows the original (P1 section 6.9): the rules INI (then any loose
// RULE*.INI overrides), then art, AI, sound, EVA and theme files. Object types are
// registered in the order they are listed in the rules INI. A map can add to and
// override the rules (apply_map).
//
// Reading values: object() returns a RuleObject that reads typed values with the
// original's defaults (rules/rule_object.h).

#include "formats/csf.h"
#include "rules/game_def.h"
#include "rules/rule_object.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra::rules {

// The kinds of object types listed in rules sections such as [InfantryTypes].
enum class TypeKind {
	Country, Infantry, Vehicle, Aircraft, Building, Terrain, Smudge, Overlay,
	Animation, VoxelAnim, Particle, ParticleSystem, SuperWeapon, Warhead, Tiberium,
};
inline constexpr int kTypeKindCount = 15;

const char* list_section_name(TypeKind kind);   // "InfantryTypes"
const char* owner_class_name(TypeKind kind);    // "InfantryTypeClass"

// The files a RulesDb is built from, as text. Missing optional files are empty.
struct RulesSources {
	std::string_view rules;
	std::vector<std::string_view> rules_overrides; // loose RULE*.INI files, applied after 'rules'
	std::string_view art;
	std::string_view ai;
	std::string_view sound;
	std::string_view eva;
	std::string_view theme;
};

class RulesDb {
public:
	// Reads the game's files through the VFS. Returns false if the rules or art file is missing.
	bool load(const ra::vfs::Vfs& vfs, GameKind kind, std::string* error = nullptr);
	void load_from_text(GameKind kind, const RulesSources& sources);

	GameKind game() const { return game_; }
	const ra::ini::IniFile& rules() const { return rules_; }
	const ra::ini::IniFile& art() const { return art_; }
	const ra::ini::IniFile& ai() const { return ai_; }
	const ra::ini::IniFile& sound() const { return sound_; }
	const ra::ini::IniFile& eva() const { return eva_; }
	const ra::ini::IniFile& theme() const { return theme_; }

	// ---- the registry of types ----
	// Names in registration order. An object's index in this list is its type index
	// (for example the byte stored in a map's overlay data).
	const std::vector<std::string>& type_names(TypeKind kind) const { return types_[static_cast<size_t>(kind)]; }
	int type_count(TypeKind kind) const { return static_cast<int>(type_names(kind).size()); }
	// -1 if there is no such type. Names are case-insensitive.
	int type_index(TypeKind kind, std::string_view name) const;
	std::string_view type_name(TypeKind kind, int index) const;

	// ---- reading values ----
	RuleObject object(TypeKind kind, int index) const;
	RuleObject object(TypeKind kind, std::string_view name) const;
	RuleObject weapon(std::string_view name) const { return section(name, "WeaponTypeClass"); }
	RuleObject projectile(std::string_view name) const { return section(name, "BulletTypeClass"); }
	// Any rules section: [General], [CombatDamage], [AudioVisual]... with the class that reads it.
	RuleObject section(std::string_view name, std::string_view owner_class = "RulesClass") const;
	// A section of the art file.
	RuleObject art_section(std::string_view name, std::string_view owner_class = "") const;
	// The art section an object uses: its Image= value, or its own name.
	std::string image_name(const RuleObject& object) const;

	// ---- maps ----
	// Applies the rules a map carries: its sections override and extend the rules,
	// and new types it lists are registered. Sections that only hold map data
	// (terrain, units, triggers...) are left alone.
	void apply_map(const ra::ini::IniFile& map);

	// Strings for a name, or the name itself if it isn't in the string table.
	void set_strings(ra::formats::CsfFile strings) { strings_ = std::move(strings); }
	const ra::formats::CsfFile& strings() const { return strings_; }

private:
	void rebuild_registry();
	void add_type_names_from(const ra::ini::IniFile& ini, bool replace);

	GameKind game_ = GameKind::YurisRevenge;
	ra::ini::IniFile rules_, art_, ai_, sound_, eva_, theme_;
	std::vector<std::string> types_[kTypeKindCount];
	std::unordered_map<std::string, int> type_lookup_[kTypeKindCount]; // lower-case name -> index
	ra::formats::CsfFile strings_;
};

} // namespace ra::rules
