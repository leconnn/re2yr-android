#pragma once

// Westwood bitmap fonts (P1 section 6.1). Two layouts exist: "fonT" with a direct
// 65,536-entry code point table, and "FoNt" with a table of code point ranges.
// A glyph is a fixed-size record: one width byte, then the bitmap rows.

#include <cstdint>
#include <span>

namespace ra::formats {

class FntFile {
public:
	// 'data' must outlive the FntFile.
	static bool parse(std::span<const uint8_t> data, FntFile& out);

	uint32_t max_width() const { return max_width_; }
	uint32_t height() const { return height_; }
	uint32_t line_height() const { return line_height_; }
	uint32_t bytes_per_row() const { return bytes_per_row_; }
	uint32_t glyph_count() const { return glyph_count_; }

	// The raw record (1 + bytes_per_row * height bytes) for a code point, or
	// empty if the font has no glyph for it.
	std::span<const uint8_t> glyph(uint32_t code_point) const;

private:
	std::span<const uint8_t> data_;
	uint32_t max_width_ = 0, height_ = 0, line_height_ = 0, bytes_per_row_ = 0, glyph_count_ = 0;
	uint32_t glyph_size_ = 0;
	size_t glyph_data_ = 0;
	bool direct_ = false;
	size_t table_offset_ = 0;  // direct: 65536 u16 glyph numbers; ranged: range records
	uint32_t range_count_ = 0;
};

} // namespace ra::formats
