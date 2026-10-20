#include "formats/palette.h"

namespace ra::formats {

bool parse_palette(std::span<const uint8_t> data, Palette& out)
{
	if (data.size() != 768)
		return false;
	for (size_t i = 0; i < 256; ++i) {
		for (int c = 0; c < 3; ++c) {
			if (data[i * 3 + c] > 63)
				return false;
		}
		out.colors[i] = { widen6(data[i * 3]), widen6(data[i * 3 + 1]), widen6(data[i * 3 + 2]) };
	}
	return true;
}

} // namespace ra::formats
