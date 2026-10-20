#include "render/indexed_atlas.h"

#include "core/assert.h"

#include <algorithm>
#include <cstring>

namespace ra::render {

IndexedAtlas::~IndexedAtlas()
{
	for (Page& p : pages_) {
		if (p.texture)
			gl().glDeleteTextures(1, &p.texture);
	}
}

IndexedAtlas::Page& IndexedAtlas::page_with_room(int width, int height)
{
	RA_ASSERT_MSG(width <= page_size_ && height <= page_size_, "image larger than an atlas page");
	if (!pages_.empty()) {
		Page& p = pages_.back();
		// Start a new row of the shelf when this one is full.
		if (p.shelf_x + width > page_size_) {
			p.shelf_y += p.shelf_height;
			p.shelf_x = 0;
			p.shelf_height = 0;
		}
		if (p.shelf_y + height <= page_size_)
			return p;
	}
	pages_.emplace_back();
	Page& p = pages_.back();
	p.pixels.assign(size_t(page_size_) * page_size_ * 4, 0);
	return p;
}

AtlasRect IndexedAtlas::add(int width, int height, const uint8_t* indices, const uint8_t* coverage, const uint8_t* depth)
{
	AtlasRect r;
	if (width <= 0 || height <= 0)
		return r;
	Page& p = page_with_room(width, height);
	r.page = static_cast<int>(&p - pages_.data());
	r.x = p.shelf_x;
	r.y = p.shelf_y;
	r.width = width;
	r.height = height;
	for (int y = 0; y < height; ++y) {
		uint8_t* row = &p.pixels[(size_t(r.y + y) * page_size_ + r.x) * 4];
		for (int x = 0; x < width; ++x) {
			const size_t i = size_t(y) * width + x;
			row[x * 4 + 0] = indices[i];
			row[x * 4 + 1] = (coverage ? coverage[i] != 0 : indices[i] != 0) ? 255 : 0;
			row[x * 4 + 2] = depth ? depth[i] : 0;
			row[x * 4 + 3] = 255;
		}
	}
	p.shelf_x += width;
	p.shelf_height = std::max(p.shelf_height, height);
	if (p.dirty_bottom < 0) {
		p.dirty_top = r.y;
		p.dirty_bottom = r.y + height - 1;
	} else {
		p.dirty_top = std::min(p.dirty_top, r.y);
		p.dirty_bottom = std::max(p.dirty_bottom, r.y + height - 1);
	}
	++images_;
	return r;
}

void IndexedAtlas::upload()
{
	const GlApi& g = gl();
	for (Page& p : pages_) {
		if (p.dirty_bottom < 0)
			continue;
		if (!p.created) {
			g.glGenTextures(1, &p.texture);
			g.glBindTexture(GL_TEXTURE_2D, p.texture);
			g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			g.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			g.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, page_size_, page_size_, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.pixels.data());
			p.created = true;
		} else {
			g.glBindTexture(GL_TEXTURE_2D, p.texture);
			g.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			const int rows = p.dirty_bottom - p.dirty_top + 1;
			g.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, p.dirty_top, page_size_, rows, GL_RGBA, GL_UNSIGNED_BYTE,
			                  &p.pixels[size_t(p.dirty_top) * page_size_ * 4]);
		}
		p.dirty_top = 0;
		p.dirty_bottom = -1;
	}
}

} // namespace ra::render
