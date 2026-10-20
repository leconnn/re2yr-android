#include "formats/shp.h"

namespace ra::formats {

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

} // namespace

bool ShpFile::parse(std::span<const uint8_t> data, ShpFile& out)
{
	if (data.size() < 8)
		return false;
	const uint16_t zero = rd16(data.data());
	const uint16_t width = rd16(data.data() + 2);
	const uint16_t height = rd16(data.data() + 4);
	const uint16_t count = rd16(data.data() + 6);
	if (zero != 0 || count == 0 || width > 2048 || height > 2048)
		return false;
	if (data.size() < 8 + 24ull * count)
		return false;

	out.data_ = data;
	out.width_ = width;
	out.height_ = height;
	out.frames_.clear();
	out.frames_.reserve(count);
	for (uint16_t i = 0; i < count; ++i) {
		const uint8_t* p = data.data() + 8 + 24ull * i;
		ShpFrame f;
		f.x = rd16(p);
		f.y = rd16(p + 2);
		f.width = rd16(p + 4);
		f.height = rd16(p + 6);
		f.flags = rd32(p + 8);
		f.radar_color = rd32(p + 12);
		f.offset = rd32(p + 20);
		if (!f.empty() && (uint32_t(f.x) + f.width > width || uint32_t(f.y) + f.height > height))
			return false;
		out.frames_.push_back(f);
	}
	return true;
}

bool ShpFile::decode_frame(size_t index, std::vector<uint8_t>& pixels) const
{
	pixels.clear();
	if (index >= frames_.size())
		return false;
	const ShpFrame& f = frames_[index];
	if (f.empty())
		return true;

	const size_t w = f.width, h = f.height;
	pixels.assign(w * h, 0);
	const uint8_t compression = static_cast<uint8_t>(f.flags & 0xFF);
	size_t p = f.offset;

	if (compression == 0 || compression == 1) {
		if (p + w * h > data_.size())
			return false;
		for (size_t i = 0; i < w * h; ++i)
			pixels[i] = data_[p + i];
		return true;
	}
	if (compression != 2 && compression != 3)
		return false;

	for (size_t y = 0; y < h; ++y) {
		if (p + 2 > data_.size())
			return false;
		const size_t line_len = rd16(data_.data() + p);
		if (line_len < 2 || p + line_len > data_.size())
			return false;
		size_t q = p + 2;
		const size_t end = p + line_len;
		uint8_t* row = pixels.data() + y * w;
		if (compression == 2) {
			// Raw pixels for the whole line.
			if (line_len - 2 != w)
				return false;
			for (size_t x = 0; x < w; ++x)
				row[x] = data_[q + x];
		} else {
			size_t x = 0;
			while (q < end) {
				const uint8_t b = data_[q++];
				if (b == 0) {
					if (q >= end)
						return false;
					x += data_[q++]; // transparent run
				} else {
					if (x < w)
						row[x] = b;
					++x;
				}
			}
			// A line may end with a transparent run reaching past the frame width.
			if (x < w)
				return false;
		}
		p = end;
	}
	return true;
}

} // namespace ra::formats
