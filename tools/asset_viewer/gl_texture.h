#pragma once

// A tiny RGBA texture helper for the tools, loading the few OpenGL functions it
// needs at run time so it works with both desktop OpenGL and OpenGL ES.

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace viewer {

class Texture {
public:
	Texture() = default;
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;
	~Texture() { release(); }

	// Uploads width * height RGBA pixels (row 0 at the top).
	void upload(int width, int height, const uint8_t* rgba);
	void release();

	uint32_t id() const { return id_; }
	int width() const { return width_; }
	int height() const { return height_; }
	bool valid() const { return id_ != 0; }

private:
	uint32_t id_ = 0;
	int width_ = 0, height_ = 0;
};

} // namespace viewer
