#pragma once

// The OpenGL functions the renderer uses, loaded at run time through SDL so the same code
// runs on desktop OpenGL 3.3 and on OpenGL ES 3.0 (Android). Call load_gl() once after a
// context is current.

#include <cstddef>
#include <cstdint>

namespace ra::render {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

enum : GLenum {
	GL_FALSE = 0, GL_TRUE = 1,
	GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x100,
	GL_TRIANGLES = 0x0004, GL_FLOAT = 0x1406, GL_UNSIGNED_BYTE = 0x1401, GL_UNSIGNED_SHORT = 0x1403,
	GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893, GL_DYNAMIC_DRAW = 0x88E8, GL_STATIC_DRAW = 0x88E4,
	GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
	GL_INFO_LOG_LENGTH = 0x8B84,
	GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0, GL_TEXTURE1 = 0x84C1, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058,
	GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
	GL_NEAREST = 0x2600, GL_CLAMP_TO_EDGE = 0x812F, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05,
	GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71, GL_CULL_FACE = 0x0B44, GL_SCISSOR_TEST = 0x0C11,
	GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_ONE = 1, GL_LESS = 0x0201, GL_LEQUAL = 0x0203,
	GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01, GL_MAX_TEXTURE_SIZE = 0x0D33,
};

struct GlApi {
	void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	void (*glClear)(GLbitfield);
	void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
	void (*glEnable)(GLenum);
	void (*glDisable)(GLenum);
	void (*glBlendFunc)(GLenum, GLenum);
	void (*glDepthFunc)(GLenum);
	void (*glScissor)(GLint, GLint, GLsizei, GLsizei);
	const unsigned char* (*glGetString)(GLenum);
	void (*glGetIntegerv)(GLenum, GLint*);

	GLuint (*glCreateShader)(GLenum);
	void (*glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
	void (*glCompileShader)(GLuint);
	void (*glGetShaderiv)(GLuint, GLenum, GLint*);
	void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
	void (*glDeleteShader)(GLuint);
	GLuint (*glCreateProgram)();
	void (*glAttachShader)(GLuint, GLuint);
	void (*glLinkProgram)(GLuint);
	void (*glGetProgramiv)(GLuint, GLenum, GLint*);
	void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
	void (*glDeleteProgram)(GLuint);
	void (*glUseProgram)(GLuint);
	GLint (*glGetUniformLocation)(GLuint, const GLchar*);
	GLint (*glGetAttribLocation)(GLuint, const GLchar*);
	void (*glUniform1i)(GLint, GLint);
	void (*glUniform1f)(GLint, GLfloat);
	void (*glUniform2f)(GLint, GLfloat, GLfloat);
	void (*glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);

	void (*glGenBuffers)(GLsizei, GLuint*);
	void (*glDeleteBuffers)(GLsizei, const GLuint*);
	void (*glBindBuffer)(GLenum, GLuint);
	void (*glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
	void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
	void (*glGenVertexArrays)(GLsizei, GLuint*);
	void (*glDeleteVertexArrays)(GLsizei, const GLuint*);
	void (*glBindVertexArray)(GLuint);
	void (*glEnableVertexAttribArray)(GLuint);
	void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
	void (*glDrawArrays)(GLenum, GLint, GLsizei);

	void (*glGenTextures)(GLsizei, GLuint*);
	void (*glDeleteTextures)(GLsizei, const GLuint*);
	void (*glBindTexture)(GLenum, GLuint);
	void (*glActiveTexture)(GLenum);
	void (*glTexParameteri)(GLenum, GLenum, GLint);
	void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
	void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
	void (*glPixelStorei)(GLenum, GLint);
	void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
};

// Loads every function. Returns false (and logs) if one is missing.
bool load_gl();
const GlApi& gl();

// True when the context is OpenGL ES (shaders then need "#version 300 es" and precision lines).
void set_gl_is_es(bool es);
bool gl_is_es();

} // namespace ra::render
