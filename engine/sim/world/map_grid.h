#pragma once

// The cell grid of a map and what each cell holds (P3 section 8).

#include "core/coord.h"
#include "sim/world/land.h"

#include <cstdint>
#include <vector>

namespace ra::world {

struct MapCell {
	bool exists = false;           // the cell is inside the map diamond (cells outside it do not exist)
	int32_t tile = -1;             // template index, -1 if none
	uint8_t sub_tile = 0;          // which cell of the template
	int8_t level = 0;              // height level
	uint8_t slope = 0;             // 0 flat, 1..20 ramp shape (SlopeIndex)
	LandType land = LandType::Clear;
	int16_t overlay = -1;          // overlay type index, -1 none
	uint8_t overlay_data = 0;      // ore stage, wall frame, vein stage...
};

class MapGrid {
public:
	static constexpr int kSize = 512; // cells are stored on a 512 x 512 grid, index = y * 512 + x

	MapGrid() : cells_(size_t(kSize) * kSize) {}

	// Off-map coordinates read a blank dummy cell, as in the original.
	const MapCell& at(int x, int y) const { return in_grid(x, y) ? cells_[size_t(y) * kSize + x] : dummy_; }
	const MapCell& at(ra::Cell c) const { return at(c.x, c.y); }
	MapCell* mutable_at(int x, int y) { return in_grid(x, y) ? &cells_[size_t(y) * kSize + x] : nullptr; }

	static bool in_grid(int x, int y) { return x >= 0 && y >= 0 && x < kSize && y < kSize; }

	// The playable diamond. width / height are the map's Size (MapRect); local_* its LocalSize.
	void set_size(int width, int height, int local_x, int local_y, int local_width, int local_height);
	int width() const { return width_; }
	int height() const { return height_; }
	// Whether a cell is inside the map's diamond.
	bool in_map(int x, int y) const;
	// Whether a cell is inside the playable (local) area (MapClass::IsWithinUsableArea, 0x578460). With
	// 'by_level' the bottom edge moves down by the cell's height (one more for a slope near the top edge).
	bool in_local(int x, int y, bool by_level = false) const;

	// The height of the ground at a lepton position, for the cell it lies in.
	int ground_height(const ra::Coord& c) const;

private:
	std::vector<MapCell> cells_;
	MapCell dummy_;
	int width_ = 0, height_ = 0;
	int local_x_ = 0, local_y_ = 0, local_width_ = 0, local_height_ = 0;
};

} // namespace ra::world
