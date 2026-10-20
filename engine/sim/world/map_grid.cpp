#include "sim/world/map_grid.h"

namespace ra::world {

void MapGrid::set_size(int width, int height, int local_x, int local_y, int local_width, int local_height)
{
	width_ = width;
	height_ = height;
	local_x_ = local_x;
	local_y_ = local_y;
	local_width_ = local_width;
	local_height_ = local_height;
}

namespace {

// The diamond test of MapClass: with W and H the rectangle's width and height, a cell is
// inside when x + y > W, x - y < W, y - x < W and x + y <= W + 2H.
bool in_diamond(int x, int y, int w, int h)
{
	return x + y > w && x - y < w && y - x < w && x + y <= w + 2 * h;
}

} // namespace

bool MapGrid::in_map(int x, int y) const
{
	return in_diamond(x, y, width_, height_);
}

bool MapGrid::in_local(int x, int y) const
{
	// IsWithinUsableArea applies the same test to LocalSize.
	return in_diamond(x, y, local_width_, local_height_);
}

int MapGrid::ground_height(const ra::Coord& c) const
{
	const ra::Cell cell = ra::cell_of(c);
	const MapCell& m = at(cell);
	return floor_height(m.level, m.slope, c.x & 255, c.y & 255);
}

} // namespace ra::world
