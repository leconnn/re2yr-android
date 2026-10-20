#include "render/gl_api.h"

#include "core/log.h"

#include <SDL3/SDL.h>

namespace ra::render {

namespace {

GlApi g_api {};
bool g_loaded = false;
bool g_es = false;

template <typename F>
bool load(F& out, const char* name)
{
	out = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
	if (!out)
		RA_LOG_ERROR("OpenGL function %s is missing", name);
	return out != nullptr;
}

} // namespace

bool load_gl()
{
	GlApi& a = g_api;
	bool ok = true;
#define RA_LOAD(fn) ok &= load(a.fn, #fn)
	RA_LOAD(glClearColor); RA_LOAD(glClear); RA_LOAD(glViewport); RA_LOAD(glEnable); RA_LOAD(glDisable);
	RA_LOAD(glBlendFunc); RA_LOAD(glDepthFunc); RA_LOAD(glScissor); RA_LOAD(glGetString); RA_LOAD(glGetIntegerv);
	RA_LOAD(glCreateShader); RA_LOAD(glShaderSource); RA_LOAD(glCompileShader); RA_LOAD(glGetShaderiv);
	RA_LOAD(glGetShaderInfoLog); RA_LOAD(glDeleteShader); RA_LOAD(glCreateProgram); RA_LOAD(glAttachShader);
	RA_LOAD(glLinkProgram); RA_LOAD(glGetProgramiv); RA_LOAD(glGetProgramInfoLog); RA_LOAD(glDeleteProgram);
	RA_LOAD(glUseProgram); RA_LOAD(glGetUniformLocation); RA_LOAD(glGetAttribLocation); RA_LOAD(glUniform1i);
	RA_LOAD(glUniform1f); RA_LOAD(glUniform2f); RA_LOAD(glUniform4f);
	RA_LOAD(glGenBuffers); RA_LOAD(glDeleteBuffers); RA_LOAD(glBindBuffer); RA_LOAD(glBufferData);
	RA_LOAD(glBufferSubData); RA_LOAD(glGenVertexArrays); RA_LOAD(glDeleteVertexArrays); RA_LOAD(glBindVertexArray);
	RA_LOAD(glEnableVertexAttribArray); RA_LOAD(glVertexAttribPointer); RA_LOAD(glDrawArrays);
	RA_LOAD(glGenTextures); RA_LOAD(glDeleteTextures); RA_LOAD(glBindTexture); RA_LOAD(glActiveTexture);
	RA_LOAD(glTexParameteri); RA_LOAD(glTexImage2D); RA_LOAD(glTexSubImage2D); RA_LOAD(glPixelStorei); RA_LOAD(glReadPixels);
#undef RA_LOAD
	g_loaded = ok;
	return ok;
}

const GlApi& gl()
{
	return g_api;
}

void set_gl_is_es(bool es)
{
	g_es = es;
}

bool gl_is_es()
{
	return g_es;
}

} // namespace ra::render
