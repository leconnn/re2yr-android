#pragma once

// World coordinates.
//
// Positions are in leptons, 256 per cell. A cell is addressed by 16-bit
// (x, y) indices. Height (z) is in leptons too; one height level is 104
// leptons (Unsorted::LevelHeight).

#include <cstdint>

namespace ra {

inline constexpr int kLeptonsPerCell = 256;
inline constexpr int kLeptonsPerHalfCell = 128;
inline constexpr int kLeptonsPerLevel = 104;

struct Cell {
	int16_t x = 0;
	int16_t y = 0;

	friend constexpr bool operator==(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }
	friend constexpr bool operator!=(Cell a, Cell b) { return !(a == b); }
	friend constexpr Cell operator+(Cell a, Cell b) { return { int16_t(a.x + b.x), int16_t(a.y + b.y) }; }
	friend constexpr Cell operator-(Cell a, Cell b) { return { int16_t(a.x - b.x), int16_t(a.y - b.y) }; }
};

struct Coord {
	int32_t x = 0;
	int32_t y = 0;
	int32_t z = 0;

	friend constexpr bool operator==(const Coord& a, const Coord& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
	friend constexpr bool operator!=(const Coord& a, const Coord& b) { return !(a == b); }
	friend constexpr Coord operator+(const Coord& a, const Coord& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	friend constexpr Coord operator-(const Coord& a, const Coord& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
};

// The cell containing a coordinate. Uses division (truncation toward zero), as the original does.
constexpr Cell cell_of(const Coord& c)
{
	return { static_cast<int16_t>(c.x / kLeptonsPerCell), static_cast<int16_t>(c.y / kLeptonsPerCell) };
}

// The centre of a cell at the given height.
constexpr Coord cell_center(Cell c, int32_t z = 0)
{
	return { c.x * kLeptonsPerCell + kLeptonsPerHalfCell, c.y * kLeptonsPerCell + kLeptonsPerHalfCell, z };
}

// The cell's top-left corner (its lowest x and y lepton).
constexpr Coord cell_origin(Cell c, int32_t z = 0)
{
	return { c.x * kLeptonsPerCell, c.y * kLeptonsPerCell, z };
}

} // namespace ra
