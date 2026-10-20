#pragma once

// The table of every INI key the original game reads (about 2,200).
//
// Each entry says which class reads the key, which section kind it lives in, how its
// value is read, and the built-in default used when the INI doesn't supply it
// (P2 section 2). It is generated from re/data/ini_keys.tsv by
// tools/gen_rules_keys.py.

#include <cstddef>
#include <span>
#include <string_view>

namespace ra::rules {

enum class KeyType {
	Int, Bool, String, Double, TypeList, Leptons, Rgb, Movie, InfantryList, House, BuildingTypeList, BuildingList,
	Type, Rect, ColorScheme, Theater, SoundList, UnitList, TechnoList, HouseBits, Armor, AircraftList,
};

struct KeyInfo {
	const char* owner;        // class whose Read_INI reads it, "" if it couldn't be attributed
	const char* key;
	const char* section;      // "<object id>" for a type's own section, otherwise the section name
	KeyType type;
	const char* default_text; // the default as text, in the type's own format
	bool has_default;         // false when the default isn't known (treat as zero / empty)
};

class KeyTable {
public:
	static const KeyTable& instance();

	// Finds the entry for 'key' as read by 'owner_class', looking through the class's
	// bases first and unattributed keys last. Key and class names are case-insensitive.
	const KeyInfo* find(std::string_view owner_class, std::string_view key) const;

	std::span<const KeyInfo> all() const;
	size_t size() const { return all().size(); }

private:
	KeyTable();
	struct Impl;
	Impl* impl_;
};

// The class a class's own keys are inherited from ("" at the root).
std::string_view base_class(std::string_view owner_class);

} // namespace ra::rules
