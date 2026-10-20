#pragma once

// TMP isometric terrain templates (P1 section 6.3).
//
// A template is a grid of up to blocks_w x blocks_h tiles; some slots are
// empty. Each tile is a 60x30 diamond of palette indices (900 bytes: 29 rows,
// 4, 8 ... 60, then 56 ... 8, 4 pixels wide), optional per-pixel depth, and an
// optional "extra" rectangle for overhanging pixels such as cliff faces.

#include "formats/palette.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ra::formats {

inline constexpr int kTileWidth = 60;
inline constexpr int kTileHeight = 30;
inline constexpr int kTilePixels = 900;

// Width in pixels of row 'y' (0..29) of the diamond: 4, 8 ... 60, then 56 ... 8, 4, and 0 for the
// last row (900 pixels in all). Diagonal neighbours interlock, so tiles leave no gaps.
constexpr int tile_row_width(int y) { return y < 15 ? 4 * (y + 1) : 4 * (29 - y); }

struct TmpTile {
	bool present = false;
	int32_t x = 0, y = 0;                        // position of the diamond, relative to the template
	uint32_t flags = 0;                          // bit 0 has extra, bit 1 has depth data, bit 2 has damaged variant
	uint8_t height = 0;                          // height level
	uint8_t land_type = 0;
	uint8_t ramp_type = 0;
	Rgb radar_left, radar_right;
	std::span<const uint8_t> image;              // 900 palette indices
	std::span<const uint8_t> depth;              // 900 depth values, or empty
	int32_t extra_x = 0, extra_y = 0;            // top-left of the extra rectangle
	uint32_t extra_width = 0, extra_height = 0;
	std::span<const uint8_t> extra;              // extra_width * extra_height palette indices, or empty
	std::span<const uint8_t> extra_depth;        // same size, or empty

	bool has_extra() const { return (flags & 1) != 0; }
	bool has_depth() const { return (flags & 2) != 0; }
};

class TmpFile {
public:
	// 'data' must outlive the TmpFile: tile images point into it.
	static bool parse(std::span<const uint8_t> data, TmpFile& out);

	uint32_t blocks_wide() const { return blocks_w_; }
	uint32_t blocks_high() const { return blocks_h_; }
	// blocks_wide * blocks_high slots, in row-major order.
	const std::vector<TmpTile>& tiles() const { return tiles_; }
	size_t present_count() const;

private:
	uint32_t blocks_w_ = 0, blocks_h_ = 0;
	std::vector<TmpTile> tiles_;
};

// Expands a tile's diamond into a 60x30 buffer of palette indices (0 where
// the diamond doesn't cover). 'pixels' is resized to 1800 entries.
void expand_tile_diamond(std::span<const uint8_t> image, std::vector<uint8_t>& pixels);

} // namespace ra::formats
