#include "sim/world/shroud.h"

#include "core/fastmath.h"
#include "core/fpu.h"
#include "sim/world/land.h"

#include <algorithm>
#include <cstdlib>

namespace ra::world {

namespace {

struct Offset {
	int8_t dx, dy;
};
// The hand-made circle table of MapClass (0xABD490); radius r uses the first kRevealCount[r] entries.
const Offset kRevealOffsets[] = {
#include "sim/world/reveal_offsets.inc"
};
// The per-entry line of sight steps (0xABCF60) used with RevealByHeight.
const Offset kLosOffsets[] = {
#include "sim/world/reveal_los.inc"
};
const int kRevealCount[12] = { 1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309, 369 };

} // namespace

int reveal_cell_count(int radius) { return kRevealCount[radius < 0 ? 0 : (radius > 11 ? 11 : radius)]; }

void reveal_cell_offset(int index, int& dx, int& dy)
{
	dx = kRevealOffsets[index].dx;
	dy = kRevealOffsets[index].dy;
}

void ShroudMap::reveal_area(int cx, int cy, int radius)
{
	radius = std::min(radius, 10);
	if (radius < 0)
		return;
	for (int i = 0; i < kRevealCount[radius]; ++i) {
		const int dx = kRevealOffsets[i].dx, dy = kRevealOffsets[i].dy;
		const int x = cx + dx, y = cy + dy;
		if (!MapGrid::in_grid(x, y))
			continue;
		// Each cell must also pass |dx| <= r and ftol(sqrt(dx^2 + dy^2)) <= r.
		if (std::abs(dx) > radius || ra::fpu::ftol(ra::fastmath::sqrt(double(dx * dx + dy * dy))) > radius)
			continue;
		revealed_[size_t(y) * MapGrid::kSize + size_t(x)] = 1;
	}
}

void ShroudMap::reveal_from(const MapGrid& grid, ra::Coord location, int radius, bool reveal_by_height)
{
	// The height shift: the centre moves up the screen by zpix(Z) / 30 cells on both axes.
	const int shift = ra::world::height_to_pixels(location.z) / 30;
	const int cx = (location.x - shift * 256) / 256, cy = (location.y - shift * 256) / 256;
	// Rejected when the centre is outside the map diamond.
	if (!grid.in_map(cx, cy) || radius == 0)
		return;
	radius = std::min(radius, 10);
	const int viewer_level = location.z / ra::world::kLevelHeight;
	for (int k = 0; k < kRevealCount[radius]; ++k) {
		const int dx = kRevealOffsets[k].dx, dy = kRevealOffsets[k].dy;
		const int x = cx + dx, y = cy + dy;
		if (!grid.in_map(x, y))
			continue;
		if (std::abs(dx) > radius || ra::fpu::ftol(ra::fastmath::sqrt(double(dx * dx + dy * dy))) > radius)
			continue;
		if (reveal_by_height) {
			// The probe cell: the target, the line of sight step and the height shift (+2).
			const int px = kLosOffsets[k].dx + x + shift + 2, py = kLosOffsets[k].dy + y + shift + 2;
			if (viewer_level + 3 < int(grid.at(px, py).level))
				continue;
		}
		revealed_[size_t(y) * MapGrid::kSize + size_t(x)] = 1;
	}
}

} // namespace ra::world
