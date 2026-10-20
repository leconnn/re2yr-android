#include "formats/pcx.h"

namespace ra::formats {

namespace {

// Decodes one scanline of run-length data (all planes) into 'line'. Returns false on truncated data.
bool decode_line(std::span<const uint8_t> data, size_t& p, size_t limit, std::vector<uint8_t>& line)
{
	size_t x = 0;
	while (x < line.size()) {
		if (p >= limit)
			return false;
		uint8_t b = data[p++];
		unsigned run = 1;
		if ((b & 0xC0) == 0xC0) {
			run = b & 0x3F;
			if (p >= limit)
				return false;
			b = data[p++];
		}
		for (unsigned k = 0; k < run && x < line.size(); ++k, ++x)
			line[x] = b;
	}
	return true;
}

} // namespace

bool parse_pcx(std::span<const uint8_t> data, PcxImage& out)
{
	if (data.size() < 128 || data[0] != 0x0A || data[2] != 1 || data[3] != 8)
		return false;
	const unsigned xmin = data[4] | (data[5] << 8), ymin = data[6] | (data[7] << 8);
	const unsigned xmax = data[8] | (data[9] << 8), ymax = data[10] | (data[11] << 8);
	const unsigned planes = data[65];
	const unsigned bytes_per_line = data[66] | (data[67] << 8);
	if ((planes != 1 && planes != 3) || xmax < xmin || ymax < ymin)
		return false;
	const unsigned width = xmax - xmin + 1, height = ymax - ymin + 1;
	if (bytes_per_line < width || width > 4096 || height > 4096)
		return false;

	size_t limit = data.size();
	if (planes == 1) {
		// The palette is the last 769 bytes: 0x0C then 256 RGB triples.
		if (data.size() < 128 + 769)
			return false;
		limit = data.size() - 769;
		if (data[limit] != 0x0C)
			return false;
	}

	out.width = width;
	out.height = height;
	out.truecolor = planes == 3;
	out.pixels.clear();
	out.rgb.clear();
	std::vector<uint8_t> line(size_t(bytes_per_line) * planes);
	size_t p = 128;
	if (planes == 1)
		out.pixels.resize(size_t(width) * height);
	else
		out.rgb.resize(size_t(width) * height * 3);
	for (unsigned y = 0; y < height; ++y) {
		if (!decode_line(data, p, limit, line))
			return false;
		if (planes == 1) {
			for (unsigned x = 0; x < width; ++x)
				out.pixels[size_t(y) * width + x] = line[x];
		} else {
			for (unsigned x = 0; x < width; ++x) {
				for (unsigned c = 0; c < 3; ++c)
					out.rgb[(size_t(y) * width + x) * 3 + c] = line[size_t(c) * bytes_per_line + x];
			}
		}
	}
	if (planes == 1) {
		for (size_t i = 0; i < 256; ++i)
			out.palette[i] = { data[limit + 1 + i * 3], data[limit + 2 + i * 3], data[limit + 3 + i * 3] };
	}
	return true;
}

} // namespace ra::formats
