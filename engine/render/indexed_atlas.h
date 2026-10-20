#pragma once

// A texture atlas of palette-indexed images.
//
// Pixels are stored as RGBA8 where R is the palette index, G is 255 where the image covers
// the pixel (0 where it is transparent), and B is a per-pixel depth value (0 if the image has
// none). Images are looked up with texelFetch, so there is no filtering and no padding.

#include "render/gl_api.h"

#include <cstdint>
#include <vector>

namespace ra::render {

struct AtlasRect {
	int page = -1;
	int x = 0, y = 0, width = 0, height = 0;
	bool valid() const { return page >= 0; }
};

class IndexedAtlas {
public:
	explicit IndexedAtlas(int page_size = 2048) : page_size_(page_size) {}
	IndexedAtlas(const IndexedAtlas&) = delete;
	IndexedAtlas& operator=(const IndexedAtlas&) = delete;
	~IndexedAtlas();

	// Adds a width * height image given as separate planes: indices, an optional coverage mask
	// (null = covered where the index is not 0) and an optional depth plane. Returns where it went.
	AtlasRect add(int width, int height, const uint8_t* indices, const uint8_t* coverage = nullptr, const uint8_t* depth = nullptr);

	// Uploads pages that changed. Call before drawing.
	void upload();

	GLuint texture(int page) const { return pages_[static_cast<size_t>(page)].texture; }
	int page_count() const { return static_cast<int>(pages_.size()); }
	int page_size() const { return page_size_; }
	size_t image_count() const { return images_; }

private:
	struct Page {
		GLuint texture = 0;
		std::vector<uint8_t> pixels;
		int shelf_x = 0, shelf_y = 0, shelf_height = 0;
		int dirty_top = 0, dirty_bottom = -1; // rows to upload, -1 = clean
		bool created = false;
	};
	Page& page_with_room(int width, int height);

	int page_size_;
	std::vector<Page> pages_;
	size_t images_ = 0;
};

} // namespace ra::render
