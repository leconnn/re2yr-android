#pragma once

// Draws palette-indexed images from an IndexedAtlas as batched quads.
//
// The pixel shader reads the palette index from the atlas, looks the colour up in a palette
// texture and multiplies it by a per-quad tint (this is how lighting and the map's colour
// cast reach the picture). Coordinates are in "world pixels" of the isometric view; the
// camera scroll and zoom are applied by the vertex shader.

#include "render/gl_util.h"
#include "render/indexed_atlas.h"

#include <vector>

namespace ra::render {

struct Tint {
	float r = 1.0f, g = 1.0f, b = 1.0f;
};

struct View {
	float scroll_x = 0.0f, scroll_y = 0.0f; // world pixel at the top-left of the screen
	float zoom = 1.0f;                      // screen pixels per world pixel
	int width = 0, height = 0;              // viewport size in screen pixels
};

class SpriteBatch {
public:
	SpriteBatch() = default;
	SpriteBatch(const SpriteBatch&) = delete;
	SpriteBatch& operator=(const SpriteBatch&) = delete;
	~SpriteBatch();

	bool init();

	void begin(const View& view);
	// Selects the palette used by the sprites drawn after this call.
	void set_palette(const PaletteTexture& palette);
	// Draws an atlas rectangle with its top-left corner at (x, y) in world pixels.
	void draw(const IndexedAtlas& atlas, const AtlasRect& rect, float x, float y, Tint tint = {}, float depth = 0.0f);
	void end();

	size_t quads_drawn() const { return quads_drawn_; }

private:
	struct Vertex {
		float x, y, u, v, r, g, b, depth;
	};
	void flush();

	ShaderProgram program_;
	GLuint vao_ = 0, vbo_ = 0;
	GLint u_scroll_ = -1, u_zoom_ = -1, u_viewport_ = -1, u_atlas_ = -1, u_palette_ = -1;
	std::vector<Vertex> vertices_;
	View view_;
	GLuint palette_ = 0;
	GLuint page_texture_ = 0;
	size_t quads_drawn_ = 0;
};

} // namespace ra::render
