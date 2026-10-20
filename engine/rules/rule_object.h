#pragma once

// Typed access to one section of the rules, with the original's defaults.
//
// A RuleObject looks like one game object (an infantry type, a weapon, a warhead...)
// to the code that reads it: each getter takes a key, reads it from the INI section
// the way the original reads that type of key, and falls back to the original's
// built-in default (from the key table) when the section doesn't have it.
//
// Reading follows the original's readers (P2 section 2):
//  - int: "$" starts a hexadecimal value; otherwise decimal, ignoring trailing text.
//  - bool: decided by the first letter: 1 T t Y y are true, 0 F f N n are false.
//  - double: parsed as a 32-bit float; a value with '%' is divided by 100 by
//    multiplying with 0.01 in the game's truncating arithmetic.
//  - leptons: the double (in cells) times 256, converted by truncation. A value of
//    exactly -1 means "not set" and gives the default.
//  - rgb: "r,g,b".

#include "formats/palette.h"
#include "ini/ini.h"

#include <string>
#include <string_view>
#include <vector>

namespace ra::rules {

struct KeyInfo;

struct RuleRect {
	int x = 0, y = 0, width = 0, height = 0;
};

class RuleObject {
public:
	RuleObject() = default;
	// 'section' may be null (an object with no section reads only defaults).
	RuleObject(const ra::ini::IniSection* section, std::string owner_class);

	bool exists() const { return section_ != nullptr; }
	const std::string& owner_class() const { return owner_; }
	std::string_view name() const { return section_ ? std::string_view(section_->name) : std::string_view(); }

	// True if the INI section itself has the key.
	bool has(std::string_view key) const;

	int get_int(std::string_view key) const;
	bool get_bool(std::string_view key) const;
	double get_double(std::string_view key) const;
	int get_leptons(std::string_view key) const;
	ra::formats::Rgb get_rgb(std::string_view key) const;
	std::string get_string(std::string_view key) const;
	// A comma-separated list with empty items removed.
	std::vector<std::string> get_list(std::string_view key) const;
	RuleRect get_rect(std::string_view key) const;

private:
	// The key's text and its table entry. text == nullptr if the INI doesn't have the key.
	const std::string* find_text(std::string_view key) const;
	const KeyInfo* info(std::string_view key) const;

	const ra::ini::IniSection* section_ = nullptr;
	std::string owner_;
};

// The readers, usable on their own. Each returns false if the text can't be read.
bool read_int(std::string_view text, int& out);
bool read_bool(std::string_view text, bool& out);
bool read_double(std::string_view text, double& out);
bool read_rgb(std::string_view text, ra::formats::Rgb& out);
bool read_rect(std::string_view text, RuleRect& out);
// A value converted to leptons the way the original does it. Returns false for "not set" (-1).
bool read_leptons(std::string_view text, int& out);

} // namespace ra::rules
