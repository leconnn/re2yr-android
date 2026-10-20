#pragma once

// CSF string tables (P1 section 6.6): labels such as "GUI:Ok" mapped to UTF-16 text.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ra::formats {

struct CsfString {
	std::u16string text;
	std::string extra; // optional extra data attached to a string (a sound name), or empty
};

class CsfFile {
public:
	static bool parse(std::span<const uint8_t> data, CsfFile& out);

	uint32_t version() const { return version_; }
	uint32_t language() const { return language_; }
	size_t label_count() const { return labels_.size(); }

	// Case-insensitive. Returns null if the label is missing.
	const CsfString* find(std::string_view label) const;

	// The first string of a label as UTF-8, or the label itself if missing.
	std::string text_or_label(std::string_view label) const;

private:
	uint32_t version_ = 0, language_ = 0;
	std::unordered_map<std::string, CsfString> labels_; // keyed by lower-case label
};

std::string utf16_to_utf8(std::u16string_view text);

} // namespace ra::formats
