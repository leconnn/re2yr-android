#include "sim/world/land.h"

#include "core/fastmath.h"
#include "core/fpu.h"
#include "ini/ini.h"

#include <string>

namespace ra::world {

namespace {

struct Plane {
	double a, b, c, max, d;
};

constexpr Plane kPlanes[kSlopeCount] = {
#include "sim/world/slope_planes.inc"
};

// The game's translation of a tile's land byte (table at 0x8288E4, entries 0..15).
constexpr LandType kTileLand[16] = {
	LandType::Clear, LandType::Ice, LandType::Ice, LandType::Ice, LandType::Ice, LandType::Tunnel,
	LandType::Railroad, LandType::Rock, LandType::Rock, LandType::Water, LandType::Beach, LandType::Road,
	LandType::Road, LandType::Clear, LandType::Rough, LandType::Rock,
};

} // namespace

// HeightScale = sin(60 degrees) * 60 / sqrt(2 * 256^2), computed at start-up (0x6D1BB0 and
// 0x6D1830) with the game's own table-based sin and sqrt in its truncating arithmetic.
double height_scale()
{
	static const double scale = [] {
		const double diagonal = ra::fastmath::sqrt(2.0 * 65536.0);
		return ra::fpu::mul(ra::fastmath::sin(1.0471975511965976), ra::fpu::div(60.0, diagonal));
	}();
	return scale;
}

LandType land_type_from_tile(uint8_t tile_land)
{
	return tile_land < 16 ? kTileLand[tile_land] : LandType::Clear;
}

LandType land_type_from_name(const char* name)
{
	static const char* const names[] = { "Clear", "Road", "Water", "Rock", "Wall", "Tiberium", "Beach", "Rough", "Ice", "Railroad", "Tunnel", "Weeds" };
	const std::string lower = ra::ini::to_lower(name);
	for (int i = 0; i < 12; ++i) {
		if (lower == ra::ini::to_lower(names[i]))
			return static_cast<LandType>(i);
	}
	return LandType::None;
}

int floor_height(int level, int slope, int x_in_cell, int y_in_cell)
{
	double z = static_cast<double>(ra::fpu::ftol(static_cast<double>(level) * kLevelHeight + 0.5));
	if (slope > 0 && slope <= kSlopeCount) {
		const Plane& p = kPlanes[slope - 1];
		// All the factors are small integers, so these products and the division by 256 are exact.
		double h = (x_in_cell & 255) * p.a * kLevelHeight / 256.0 + (y_in_cell & 255) * p.b * kLevelHeight / 256.0 + p.c + p.d;
		h = h < 0.0 ? 0.0 : (h > p.max ? p.max : h);
		z = static_cast<double>(ra::fpu::ftol(z + h));
	}
	return static_cast<int>(z);
}

int height_to_pixels(int z)
{
	const double scaled = ra::fpu::mul(static_cast<double>(z), height_scale());
	return ra::fpu::ftol(ra::fpu::add(ra::fpu::add(scaled, z >= 728 ? 1.0 : 0.0), 0.5));
}

} // namespace ra::world
