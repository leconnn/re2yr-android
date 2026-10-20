#pragma once

#include "formats/palette.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ra::formats {

// A ZSoft PCX image: 8-bit with the 256-colour palette stored after the pixels, or 24-bit (three planes per line).
struct PcxImage {
	uint32_t width = 0, height = 0;
	bool truecolor = false;          // true for 24-bit images (three planes)
	std::vector<uint8_t> pixels;     // 8-bit images: width * height palette indices
	std::array<Rgb, 256> palette {}; // 8-bit images: components as stored
	std::vector<uint8_t> rgb;        // 24-bit images: width * height * 3
};

bool parse_pcx(std::span<const uint8_t> data, PcxImage& out);

} // namespace ra::formats
