#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ra::formats {

struct Rgb {
	uint8_t r = 0, g = 0, b = 0;
};

// A 256-colour palette. The game stores 6-bit components (0..63).
struct Palette {
	std::array<Rgb, 256> colors {};
};

// Parses a .pal file (768 bytes of 6-bit RGB) and widens each component to 8 bits.
bool parse_palette(std::span<const uint8_t> data, Palette& out);

// 6-bit to 8-bit component: (v << 2) | (v >> 4).
constexpr uint8_t widen6(uint8_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }

} // namespace ra::formats
