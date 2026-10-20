#include "gl_texture.h"

namespace viewer {

namespace {

using GlGenTextures = void (*)(int, uint32_t*);
using GlDeleteTextures = void (*)(int, const uint32_t*);
using GlBindTexture = void (*)(unsigned, uint32_t);
using GlTexParameteri = void (*)(unsigned, unsigned, int);
using GlTexImage2D = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
using GlPixelStorei = void (*)(unsigned, int);

struct Gl {
	GlGenTextures gen = nullptr;
	GlDeleteTextures del = nullptr;
	GlBindTexture bind = nullptr;
	GlTexParameteri param = nullptr;
	GlTexImage2D image = nullptr;
	GlPixelStorei pixel_store = nullptr;
	bool ready = false;
};

Gl& gl()
{
	static Gl g;
	if (!g.ready) {
		g.gen = reinterpret_cast<GlGenTextures>(SDL_GL_GetProcAddress("glGenTextures"));
		g.del = reinterpret_cast<GlDeleteTextures>(SDL_GL_GetProcAddress("glDeleteTextures"));
		g.bind = reinterpret_cast<GlBindTexture>(SDL_GL_GetProcAddress("glBindTexture"));
		g.param = reinterpret_cast<GlTexParameteri>(SDL_GL_GetProcAddress("glTexParameteri"));
		g.image = reinterpret_cast<GlTexImage2D>(SDL_GL_GetProcAddress("glTexImage2D"));
		g.pixel_store = reinterpret_cast<GlPixelStorei>(SDL_GL_GetProcAddress("glPixelStorei"));
		g.ready = g.gen && g.del && g.bind && g.param && g.image && g.pixel_store;
	}
	return g;
}

constexpr unsigned kTexture2D = 0x0DE1;
constexpr unsigned kRgba = 0x1908;
constexpr unsigned kUnsignedByte = 0x1401;
constexpr unsigned kNearest = 0x2600;
constexpr unsigned kTextureMagFilter = 0x2800;
constexpr unsigned kTextureMinFilter = 0x2801;
constexpr unsigned kTextureWrapS = 0x2802;
constexpr unsigned kTextureWrapT = 0x2803;
constexpr int kClampToEdge = 0x812F;
constexpr unsigned kUnpackAlignment = 0x0CF5;

} // namespace

void Texture::upload(int width, int height, const uint8_t* rgba)
{
	Gl& g = gl();
	if (!g.ready || width <= 0 || height <= 0)
		return;
	if (!id_)
		g.gen(1, &id_);
	g.bind(kTexture2D, id_);
	g.param(kTexture2D, kTextureMinFilter, int(kNearest));
	g.param(kTexture2D, kTextureMagFilter, int(kNearest));
	g.param(kTexture2D, kTextureWrapS, kClampToEdge);
	g.param(kTexture2D, kTextureWrapT, kClampToEdge);
	g.pixel_store(kUnpackAlignment, 1);
	g.image(kTexture2D, 0, int(kRgba), width, height, 0, kRgba, kUnsignedByte, rgba);
	width_ = width;
	height_ = height;
}

void Texture::release()
{
	if (id_ && gl().ready)
		gl().del(1, &id_);
	id_ = 0;
	width_ = height_ = 0;
}

} // namespace viewer
