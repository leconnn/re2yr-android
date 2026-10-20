#pragma once

// Small OpenGL helpers: compiling a shader program and a 256-colour palette texture.

#include "formats/palette.h"
#include "render/gl_api.h"

#include <string>

namespace ra::render {

class ShaderProgram {
public:
	ShaderProgram() = default;
	ShaderProgram(const ShaderProgram&) = delete;
	ShaderProgram& operator=(const ShaderProgram&) = delete;
	~ShaderProgram() { release(); }

	// 'vertex' and 'fragment' are GLSL bodies without the #version line; the right one for
	// desktop GL 3.3 or GLES 3.0 is added. Returns false and logs the compiler output on failure.
	bool build(const char* vertex, const char* fragment);
	void release();

	GLuint id() const { return program_; }
	GLint uniform(const char* name) const { return gl().glGetUniformLocation(program_, name); }

private:
	GLuint program_ = 0;
};

// A 256 x 1 RGBA texture holding a palette multiplied by nothing; lighting is applied in the shader.
class PaletteTexture {
public:
	PaletteTexture() = default;
	PaletteTexture(const PaletteTexture&) = delete;
	PaletteTexture& operator=(const PaletteTexture&) = delete;
	~PaletteTexture() { release(); }

	void set(const ra::formats::Palette& palette);
	void release();
	GLuint id() const { return texture_; }

private:
	GLuint texture_ = 0;
};

} // namespace ra::render
