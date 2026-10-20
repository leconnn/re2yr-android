#include "sim/world/shroud.h"

#include "core/fastmath.h"
#include "core/fpu.h"

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
const int kRevealCount[12] = { 1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309, 369 };

} // namespace

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

} // namespace ra::world
