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

bool MapGrid::in_local(int x, int y, bool by_level) const
{
	// 0x578460, with W the map's width and LocalSize (lx, ly, lw, lh):
	//   W + 2 ly + h < x + y <= W + 2 + 2 (ly + lh) + h,  x - y < 2 (lx + lw) - W,  y - x < W - 2 lx,
	// where h is 0, or with 'by_level' the cell's level (+1 for a slope when x + y < W + 4 + 2 ly + level).
	int h = 0;
	if (by_level) {
		const MapCell& c = at(x, y);
		h = c.level;
		if (c.slope != 0 && x + y < width_ + 4 + 2 * local_y_ + h)
			++h;
	}
	return width_ + 2 * local_y_ + h < x + y && x + y <= width_ + 2 + 2 * (local_y_ + local_height_) + h
		&& x - y < 2 * (local_x_ + local_width_) - width_ && y - x < width_ - 2 * local_x_;
}

int MapGrid::ground_height(const ra::Coord& c) const
{
	const ra::Cell cell = ra::cell_of(c);
	const MapCell& m = at(cell);
	return floor_height(m.level, m.slope, c.x & 255, c.y & 255);
}

} // namespace ra::world
