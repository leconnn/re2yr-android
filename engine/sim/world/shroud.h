#pragma once

// The local player's shroud: which cells have been revealed (P3 section 8.2).
//
// The shroud exists only for the local player, so it is a client-side layer. A cell is either
// revealed or hidden; the soft edge between them is drawn from the revealed state of the eight
// neighbours (see render/map_renderer). Gap generators and shroud regrowth are not modelled here yet.

#include "sim/world/map_grid.h"

#include <cstdint>
#include <vector>

namespace ra::world {

// The hand-made circle table of MapClass (0xABD490) that area effects and sight both walk: the cells
// within radius r are the first reveal_cell_count(r) entries, in this order.
int reveal_cell_count(int radius);
void reveal_cell_offset(int index, int& dx, int& dy);

class ShroudMap {
public:
	ShroudMap() : revealed_(size_t(MapGrid::kSize) * MapGrid::kSize, 0) {}

	void hide_all() { revealed_.assign(revealed_.size(), 0); }
	void reveal_all() { revealed_.assign(revealed_.size(), 1); }

	// Reveals the cells of the original's circle table for a radius (at most 10), each also passing
	// |dx| <= r and ftol(sqrt(dx^2 + dy^2)) <= r (MapClass::RevealArea1 0x5673A0).
	void reveal_area(int cx, int cy, int radius);

	// RevealArea1 (0x5673A0) for an object at a lepton position: shifts the centre up the screen for
	// height, reveals the circle table around it and, with RevealByHeight, skips cells hidden behind
	// ground more than three levels above the viewer. 'radius' is the sight in cells.
	void reveal_from(const MapGrid& grid, ra::Coord location, int radius, bool reveal_by_height);

	bool revealed(int x, int y) const
	{
		return MapGrid::in_grid(x, y) && revealed_[size_t(y) * MapGrid::kSize + size_t(x)] != 0;
	}

private:
	std::vector<uint8_t> revealed_;
};

} // namespace ra::world
