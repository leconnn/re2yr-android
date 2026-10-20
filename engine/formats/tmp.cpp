#include "formats/tmp.h"

namespace ra::formats {

namespace {

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
int32_t rds32(const uint8_t* p) { return static_cast<int32_t>(rd32(p)); }

constexpr size_t kTileHeaderSize = 52;

} // namespace

bool TmpFile::parse(std::span<const uint8_t> data, TmpFile& out)
{
	if (data.size() < 16)
		return false;
	const uint32_t bw = rd32(data.data());
	const uint32_t bh = rd32(data.data() + 4);
	const uint32_t cw = rd32(data.data() + 8);
	const uint32_t ch = rd32(data.data() + 12);
	if (cw != kTileWidth || ch != kTileHeight || bw == 0 || bh == 0 || bw > 64 || bh > 64)
		return false;
	const size_t slots = size_t(bw) * bh;
	if (data.size() < 16 + 4 * slots)
		return false;

	out.blocks_w_ = bw;
	out.blocks_h_ = bh;
	out.tiles_.assign(slots, TmpTile {});
	for (size_t i = 0; i < slots; ++i) {
		const uint32_t off = rd32(data.data() + 16 + 4 * i);
		if (off == 0)
			continue;
		if (size_t(off) + kTileHeaderSize + kTilePixels > data.size())
			return false;
		const uint8_t* p = data.data() + off;
		TmpTile& t = out.tiles_[i];
		t.present = true;
		t.x = rds32(p);
		t.y = rds32(p + 4);
		const uint32_t extra_off = rd32(p + 8);
		const uint32_t depth_off = rd32(p + 12);
		const uint32_t extra_depth_off = rd32(p + 16);
		t.extra_x = rds32(p + 20);
		t.extra_y = rds32(p + 24);
		t.extra_width = rd32(p + 28);
		t.extra_height = rd32(p + 32);
		t.flags = rd32(p + 36);
		t.height = p[40];
		t.land_type = p[41];
		t.ramp_type = p[42];
		t.radar_left = { p[43], p[44], p[45] };
		t.radar_right = { p[46], p[47], p[48] };
		t.image = data.subspan(off + kTileHeaderSize, kTilePixels);

		if (t.has_depth()) {
			if (depth_off == 0 || size_t(off) + depth_off + kTilePixels > data.size())
				return false;
			t.depth = data.subspan(size_t(off) + depth_off, kTilePixels);
		}
		if (t.has_extra()) {
			const size_t extra_size = size_t(t.extra_width) * t.extra_height;
			if (extra_off == 0 || size_t(off) + extra_off + extra_size > data.size())
				return false;
			t.extra = data.subspan(size_t(off) + extra_off, extra_size);
			if (t.has_depth()) {
				if (extra_depth_off == 0 || size_t(off) + extra_depth_off + extra_size > data.size())
					return false;
				t.extra_depth = data.subspan(size_t(off) + extra_depth_off, extra_size);
			}
		}
	}
	return true;
}

size_t TmpFile::present_count() const
{
	size_t n = 0;
	for (const TmpTile& t : tiles_)
		n += t.present ? 1 : 0;
	return n;
}

void expand_tile_diamond(std::span<const uint8_t> image, std::vector<uint8_t>& pixels)
{
	pixels.assign(size_t(kTileWidth) * kTileHeight, 0);
	size_t src = 0;
	for (int y = 0; y < kTileHeight; ++y) {
		const int w = tile_row_width(y);
		const int x0 = (kTileWidth - w) / 2;
		for (int x = 0; x < w && src < image.size(); ++x)
			pixels[size_t(y) * kTileWidth + x0 + x] = image[src++];
	}
}

} // namespace ra::formats
