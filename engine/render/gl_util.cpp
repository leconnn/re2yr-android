#include "render/gl_util.h"

#include "core/log.h"

#include <vector>

namespace ra::render {

namespace {

GLuint compile(GLenum type, const std::string& source)
{
	const GlApi& g = gl();
	const GLuint shader = g.glCreateShader(type);
	const char* text = source.c_str();
	g.glShaderSource(shader, 1, &text, nullptr);
	g.glCompileShader(shader);
	GLint ok = 0;
	g.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLint length = 0;
		g.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
		g.glGetShaderInfoLog(shader, length, nullptr, log.data());
		RA_LOG_ERROR("shader compile failed: %s", log.data());
		g.glDeleteShader(shader);
		return 0;
	}
	return shader;
}

} // namespace

bool ShaderProgram::build(const char* vertex, const char* fragment)
{
	release();
	const GlApi& g = gl();
	const std::string header = gl_is_es() ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n" : "#version 330 core\n";
	const GLuint vs = compile(GL_VERTEX_SHADER, header + vertex);
	const GLuint fs = compile(GL_FRAGMENT_SHADER, header + fragment);
	if (!vs || !fs)
		return false;
	program_ = g.glCreateProgram();
	g.glAttachShader(program_, vs);
	g.glAttachShader(program_, fs);
	g.glLinkProgram(program_);
	g.glDeleteShader(vs);
	g.glDeleteShader(fs);
	GLint ok = 0;
	g.glGetProgramiv(program_, GL_LINK_STATUS, &ok);
	if (!ok) {
		GLint length = 0;
		g.glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
		g.glGetProgramInfoLog(program_, length, nullptr, log.data());
		RA_LOG_ERROR("shader link failed: %s", log.data());
		release();
		return false;
	}
	return true;
}

void ShaderProgram::release()
{
	if (program_) {
		gl().glDeleteProgram(program_);
		program_ = 0;
	}
}

void PaletteTexture::set(const ra::formats::Palette& palette)
{
	const GlApi& g = gl();
	uint8_t rgba[256 * 4];
	for (int i = 0; i < 256; ++i) {
		rgba[i * 4 + 0] = palette.colors[i].r;
		rgba[i * 4 + 1] = palette.colors[i].g;
		rgba[i * 4 + 2] = palette.colors[i].b;
		rgba[i * 4 + 3] = 255;
	}
	if (!texture_)
		g.glGenTextures(1, &texture_);
	g.glBindTexture(GL_TEXTURE_2D, texture_);
	g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	g.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	g.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	g.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void PaletteTexture::release()
{
	if (texture_) {
		gl().glDeleteTextures(1, &texture_);
		texture_ = 0;
	}
}

} // namespace ra::render
