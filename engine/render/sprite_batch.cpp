#include "render/sprite_batch.h"

#include "core/log.h"

#include <cstddef>

namespace ra::render {

namespace {

const char* kVertexShader = R"(
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_tint;
layout(location = 3) in float a_depth;
uniform vec2 u_scroll;
uniform float u_zoom;
uniform vec2 u_viewport;
out vec2 v_uv;
out vec3 v_tint;
void main()
{
	vec2 p = (a_pos - u_scroll) * u_zoom;
	gl_Position = vec4(p.x / u_viewport.x * 2.0 - 1.0, 1.0 - p.y / u_viewport.y * 2.0, a_depth, 1.0);
	v_uv = a_uv;
	v_tint = a_tint;
}
)";

const char* kFragmentShader = R"(
uniform sampler2D u_atlas;
uniform sampler2D u_palette;
in vec2 v_uv;
in vec3 v_tint;
out vec4 o_color;
void main()
{
	vec4 t = texelFetch(u_atlas, ivec2(floor(v_uv)), 0);
	if (t.g < 0.5)
		discard;
	// A negative tint marks a shadow: half-dark black over what is already there.
	if (v_tint.r < 0.0) {
		o_color = vec4(0.0, 0.0, 0.0, 0.5);
		return;
	}
	int index = int(floor(t.r * 255.0 + 0.5));
	vec3 c = texelFetch(u_palette, ivec2(index, 0), 0).rgb * v_tint;
	o_color = vec4(min(c, vec3(1.0)), 1.0);
}
)";

} // namespace

SpriteBatch::~SpriteBatch()
{
	if (gl().glDeleteBuffers && vbo_)
		gl().glDeleteBuffers(1, &vbo_);
	if (gl().glDeleteVertexArrays && vao_)
		gl().glDeleteVertexArrays(1, &vao_);
}

bool SpriteBatch::init()
{
	const GlApi& g = gl();
	if (!program_.build(kVertexShader, kFragmentShader))
		return false;
	u_scroll_ = program_.uniform("u_scroll");
	u_zoom_ = program_.uniform("u_zoom");
	u_viewport_ = program_.uniform("u_viewport");
	u_atlas_ = program_.uniform("u_atlas");
	u_palette_ = program_.uniform("u_palette");

	g.glGenVertexArrays(1, &vao_);
	g.glGenBuffers(1, &vbo_);
	g.glBindVertexArray(vao_);
	g.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	g.glBufferData(GL_ARRAY_BUFFER, 4096 * 6 * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
	const GLsizei stride = sizeof(Vertex);
	g.glEnableVertexAttribArray(0);
	g.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, x)));
	g.glEnableVertexAttribArray(1);
	g.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, u)));
	g.glEnableVertexAttribArray(2);
	g.glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, r)));
	g.glEnableVertexAttribArray(3);
	g.glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(Vertex, depth)));
	g.glBindVertexArray(0);
	vertices_.reserve(4096 * 6);
	return true;
}

void SpriteBatch::begin(const View& view)
{
	view_ = view;
	vertices_.clear();
	const GlApi& g = gl();
	g.glDisable(GL_DEPTH_TEST);
	g.glDisable(GL_CULL_FACE);
	g.glEnable(GL_BLEND);
	g.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	page_texture_ = 0;
	quads_drawn_ = 0;
}

void SpriteBatch::set_palette(const PaletteTexture& palette)
{
	if (palette_ != palette.id())
		flush();
	palette_ = palette.id();
}

void SpriteBatch::draw(const IndexedAtlas& atlas, const AtlasRect& r, float x, float y, Tint tint, float depth)
{
	if (!r.valid())
		return;
	const GLuint texture = atlas.texture(r.page);
	if (texture != page_texture_) {
		flush();
		page_texture_ = texture;
	}
	if (vertices_.size() + 6 > vertices_.capacity())
		flush();
	const float x0 = x, y0 = y, x1 = x + float(r.width), y1 = y + float(r.height);
	const float u0 = float(r.x), v0 = float(r.y), u1 = float(r.x + r.width), v1 = float(r.y + r.height);
	const Vertex quad[4] = {
		{ x0, y0, u0, v0, tint.r, tint.g, tint.b, depth },
		{ x1, y0, u1, v0, tint.r, tint.g, tint.b, depth },
		{ x1, y1, u1, v1, tint.r, tint.g, tint.b, depth },
		{ x0, y1, u0, v1, tint.r, tint.g, tint.b, depth },
	};
	for (int i : { 0, 1, 2, 0, 2, 3 })
		vertices_.push_back(quad[i]);
}

void SpriteBatch::flush()
{
	if (vertices_.empty())
		return;
	const GlApi& g = gl();
	g.glUseProgram(program_.id());
	g.glUniform2f(u_scroll_, view_.scroll_x, view_.scroll_y);
	g.glUniform1f(u_zoom_, view_.zoom);
	g.glUniform2f(u_viewport_, float(view_.width), float(view_.height));
	g.glActiveTexture(GL_TEXTURE0);
	g.glBindTexture(GL_TEXTURE_2D, page_texture_);
	g.glUniform1i(u_atlas_, 0);
	g.glActiveTexture(GL_TEXTURE1);
	g.glBindTexture(GL_TEXTURE_2D, palette_);
	g.glUniform1i(u_palette_, 1);
	g.glActiveTexture(GL_TEXTURE0);

	g.glBindVertexArray(vao_);
	g.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	g.glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)), vertices_.data());
	g.glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
	g.glBindVertexArray(0);
	quads_drawn_ += vertices_.size() / 6;
	vertices_.clear();
}

void SpriteBatch::end()
{
	flush();
}

} // namespace ra::render
