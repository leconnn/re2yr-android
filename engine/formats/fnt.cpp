#include "formats/fnt.h"

#include <cstring>

namespace ra::formats {

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

} // namespace

bool FntFile::parse(std::span<const uint8_t> data, FntFile& out)
{
	if (data.size() < 0x1C)
		return false;
	out.data_ = data;
	if (std::memcmp(data.data(), "fonT", 4) == 0) {
		out.direct_ = true;
		out.max_width_ = rd32(data.data() + 4);
		out.bytes_per_row_ = rd32(data.data() + 8);
		out.height_ = rd32(data.data() + 12);
		out.line_height_ = rd32(data.data() + 16);
		out.glyph_count_ = rd32(data.data() + 20);
		out.glyph_size_ = rd32(data.data() + 24);
		out.table_offset_ = 0x1C;
		out.glyph_data_ = 0x1C + 0x20000;
	} else if (std::memcmp(data.data(), "FoNt", 4) == 0 && data.size() >= 0x24) {
		out.direct_ = false;
		out.max_width_ = rd32(data.data() + 4);
		out.bytes_per_row_ = rd32(data.data() + 8);
		out.height_ = rd32(data.data() + 12);
		out.line_height_ = rd32(data.data() + 16);
		out.range_count_ = rd32(data.data() + 20);
		out.glyph_size_ = rd32(data.data() + 24);
		out.table_offset_ = rd32(data.data() + 28);
		out.glyph_data_ = rd32(data.data() + 32);
		out.glyph_count_ = 0;
		if (out.table_offset_ + size_t(out.range_count_) * 12 > data.size())
			return false;
		for (uint32_t i = 0; i < out.range_count_; ++i) {
			const uint8_t* r = data.data() + out.table_offset_ + 12ull * i;
			out.glyph_count_ += rd32(r + 8) - rd32(r + 4) + 1;
		}
	} else {
		return false; // legacy C&C fonts are not used by the game
	}
	if (out.glyph_size_ != 1 + out.bytes_per_row_ * out.height_)
		return false;
	return out.glyph_data_ <= data.size();
}

std::span<const uint8_t> FntFile::glyph(uint32_t code_point) const
{
	uint32_t number = 0; // 1-based glyph number, 0 = none
	if (direct_) {
		if (code_point > 0xFFFF)
			return {};
		number = rd16(data_.data() + table_offset_ + 2ull * code_point);
	} else {
		for (uint32_t i = 0; i < range_count_; ++i) {
			const uint8_t* r = data_.data() + table_offset_ + 12ull * i;
			const uint32_t start = rd32(r), first = rd32(r + 4), last = rd32(r + 8);
			if (code_point >= first && code_point <= last) {
				number = start + 1 + (code_point - first);
				break;
			}
		}
	}
	if (number == 0)
		return {};
	const size_t offset = glyph_data_ + size_t(number - 1) * glyph_size_;
	if (offset + glyph_size_ > data_.size())
		return {};
	return data_.subspan(offset, glyph_size_);
}

} // namespace ra::formats
