#pragma once

// Land types, slopes and heights (P3 sections 8.1, 8.6, 8.7; P3 section 6 for pixels).

#include <cstdint>

namespace ra::world {

enum class LandType : int8_t {
	None = -1, Clear = 0, Road = 1, Water = 2, Rock = 3, Wall = 4, Tiberium = 5,
	Beach = 6, Rough = 7, Ice = 8, Railroad = 9, Tunnel = 10, Weeds = 11,
};

// Terrain tiles store a land type byte that the game translates through a table
// (at 0x8288E4 in the executable). Unknown bytes read as Clear.
LandType land_type_from_tile(uint8_t tile_land);

// Parses a land type name as written in rules (Land=Rock). Returns None if unknown.
LandType land_type_from_name(const char* name);

inline constexpr int kLevelHeight = 104; // leptons per height level
inline constexpr int kPixelsPerLevel = 15;
inline constexpr int kSlopeCount = 20;

// The height of the ground at a point inside a cell, in leptons (CellClass::GetFloorHeight).
// 'level' is the cell's height level, 'slope' its SlopeIndex (0 flat, 1..20 ramp shapes), and
// x_in_cell / y_in_cell the lepton position inside the cell (0..255).
int floor_height(int level, int slope, int x_in_cell, int y_in_cell);

// Height in leptons to screen pixels (0x6D20E0): ftol(z * HeightScale + (z >= 728) + 0.5).
int height_to_pixels(int z);

// The scale used by height_to_pixels: pixels per lepton of height.
double height_scale();

} // namespace ra::world
