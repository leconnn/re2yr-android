#pragma once

// The game's INI dialect.
//
// - Sections are "[Name]"; entries are "key=value". Names are case-insensitive.
// - ';' starts a comment, anywhere on a line. Whitespace around keys and values is trimmed.
// - A later entry with the same key replaces the earlier one's value (keeping
//   its position), and a repeated section adds to the existing one. This is
//   how rules.ini, then art.ini, then a map's own sections override each other.
// - Lines without '=' inside a section are ignored.
//
// Typed getters follow the game's conventions: booleans are "yes/no", "true/false"
// or "1/0" (decided by the first letter); percentages may be written "50%" or
// "0.5"; integers may be written in hexadecimal with "$" (like the original, "0x" is not special).

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ra::ini {

struct IniSection {
	std::string name;
	std::vector<std::pair<std::string, std::string>> entries; // in file order
	std::unordered_map<std::string, size_t> index;            // lower-case key -> position in entries

	const std::string* find(std::string_view key) const;
	void set(std::string_view key, std::string_view value);
};

class IniFile {
public:
	// Parses INI text. Never fails: unreadable lines are skipped.
	static IniFile parse(std::string_view text);

	// Adds the sections of 'other' on top of this file: its values win.
	void merge(const IniFile& other);

	const std::vector<IniSection>& sections() const { return sections_; }
	const IniSection* section(std::string_view name) const;
	IniSection& section_mut(std::string_view name); // creates the section if missing

	const std::string* get(std::string_view section, std::string_view key) const;
	std::string get_string(std::string_view section, std::string_view key, std::string_view fallback = "") const;
	int get_int(std::string_view section, std::string_view key, int fallback = 0) const;
	double get_double(std::string_view section, std::string_view key, double fallback = 0.0) const;
	bool get_bool(std::string_view section, std::string_view key, bool fallback = false) const;
	// "50%" gives 0.5; a plain number is returned as written.
	double get_percent(std::string_view section, std::string_view key, double fallback = 0.0) const;

	// Splits a comma-separated value into trimmed items.
	static std::vector<std::string> split_list(std::string_view value);

private:
	std::vector<IniSection> sections_;
	std::unordered_map<std::string, size_t> index_; // lower-case name -> position in sections_
};

// Conversions used by the getters, exposed for the rules reader.
bool parse_int(std::string_view text, int& out);
bool parse_double(std::string_view text, double& out);
bool parse_bool(std::string_view text, bool& out);
std::string to_lower(std::string_view text);
std::string_view trim(std::string_view text);

} // namespace ra::ini
