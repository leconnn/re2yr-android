// Asset viewer: browse the game's archives and look at what is in them.
//
//   asset_viewer [--ra2] [--names names.txt] [--select name] <game folder>
//
// The game folder defaults to the RA_GAME_DIR environment variable. A names file
// (one file name per line) makes archive entries show their names; without it
// they show their hash ids. Sprites, terrain tiles, voxel models, palettes,
// pictures, maps, strings, text and sounds can be previewed with any palette.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "formats/audio.h"
#include "formats/csf.h"
#include "formats/map.h"
#include "formats/palette.h"
#include "formats/pcx.h"
#include "formats/shp.h"
#include "formats/tmp.h"
#include "formats/voxel.h"
#include "common/display.h"
#include "gl_texture.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace ra;
using namespace ra::formats;

namespace {

std::string lower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string base_name(const std::string& path)
{
	const auto slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string extension_of(const std::string& path)
{
	const std::string n = base_name(path);
	const auto dot = n.rfind('.');
	return dot == std::string::npos ? "" : lower(n.substr(dot + 1));
}

struct Entry {
	std::string path;
	std::string name;
	std::string ext;
	uint32_t size = 0;
	std::span<const uint8_t> bytes;
	bool named = false;
};

struct HouseColor {
	const char* name;
	Rgb base; // the colour of the brightest remap entry
};

const HouseColor kHouseColors[] = {
	{ "None (as stored)", { 0, 0, 0 } }, { "Gold", { 255, 210, 40 } }, { "Red", { 235, 40, 30 } },
	{ "Blue", { 50, 110, 255 } }, { "Green", { 50, 200, 70 } }, { "Orange", { 255, 130, 30 } },
	{ "Purple", { 170, 70, 220 } }, { "Teal", { 40, 200, 200 } },
};

struct Viewer {
	vfs::Vfs vfs;
	vfs::NameDb names;
	std::vector<Entry> entries;
	std::vector<int> filtered;
	std::vector<int> palette_entries; // entries that are .pal files
	char filter[128] = "";
	std::string last_filter;
	int selected = -1;
	std::string status;

	// Preview state
	Palette palette;
	int palette_choice = -1;
	std::string palette_label;
	int house_color = 0;
	int frame = 0;
	int zoom = 3;
	int voxel_yaw = 45, voxel_pitch = 25;
	int hva_frame = 0;
	viewer::Texture texture;
	std::vector<uint8_t> rgba;
	int tex_w = 0, tex_h = 0;
	bool dirty = true;

	std::unique_ptr<ShpFile> shp;
	std::unique_ptr<TmpFile> tmp;
	std::unique_ptr<VxlFile> vxl;
	std::unique_ptr<HvaFile> hva;
	std::unique_ptr<CsfFile> csf;
	std::unique_ptr<MapData> map;
	PcxImage pcx;
	PcxImage* pcx_ptr = nullptr;
	PcxImage map_preview_dummy;
	Palette pcx_palette;
	std::string text;
	char csf_label[128] = "";
	ra::vfs::FileData open_hold;
	PcmAudio audio;
	SDL_AudioStream* audio_stream = nullptr;
	std::string tmp_theater;

	void reset_preview()
	{
		shp.reset();
		tmp.reset();
		vxl.reset();
		hva.reset();
		csf.reset();
		map.reset();
		pcx_ptr = nullptr;
		text.clear();
		audio = {};
		frame = 0;
		hva_frame = 0;
		dirty = true;
		texture.release();
	}
};

void build_entries(Viewer& v, const std::string& names_path)
{
	if (!names_path.empty()) {
		std::ifstream in(names_path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		v.names.add_lines(ss.str());
	}
	v.vfs.visit_all(&v.names, [&](const vfs::Vfs::VisitedEntry& e) {
		Entry en;
		en.path = e.path;
		en.name = base_name(e.path);
		en.ext = e.named ? extension_of(e.path) : "";
		en.size = e.size;
		en.bytes = e.bytes;
		en.named = e.named;
		v.entries.push_back(std::move(en));
	});
	std::sort(v.entries.begin(), v.entries.end(), [](const Entry& a, const Entry& b) { return lower(a.path) < lower(b.path); });
	for (size_t i = 0; i < v.entries.size(); ++i) {
		if (v.entries[i].ext == "pal")
			v.palette_entries.push_back(static_cast<int>(i));
	}
}

void apply_filter(Viewer& v)
{
	v.last_filter = v.filter;
	v.filtered.clear();
	const std::string f = lower(v.filter);
	for (size_t i = 0; i < v.entries.size(); ++i) {
		if (f.empty() || lower(v.entries[i].path).find(f) != std::string::npos)
			v.filtered.push_back(static_cast<int>(i));
	}
}

bool load_palette_named(Viewer& v, const std::string& name)
{
	auto data = v.vfs.open(name);
	if (!data)
		return false;
	Palette p;
	if (!parse_palette(data.bytes, p))
		return false;
	v.palette = p;
	v.palette_label = name;
	return true;
}

// The theater of a file by extension.
struct TheaterInfo {
	const char* ext;
	const char* object_palette;
	const char* tile_palette;
};
const TheaterInfo kTheaters[] = {
	{ "tem", "temperat.pal", "isotem.pal" }, { "sno", "snow.pal", "isosno.pal" }, { "urb", "urban.pal", "isourb.pal" },
	{ "des", "desert.pal", "isodes.pal" }, { "lun", "lunar.pal", "isolun.pal" }, { "ubn", "urbann.pal", "isoubn.pal" },
};

const TheaterInfo* theater_for(const std::string& ext)
{
	for (const auto& t : kTheaters) {
		if (ext == t.ext)
			return &t;
	}
	return nullptr;
}

void choose_default_palette(Viewer& v, const Entry& e)
{
	std::string pal = "unittem.pal";
	if (const TheaterInfo* t = theater_for(e.ext)) {
		pal = (v.tmp ? t->tile_palette : t->object_palette);
	} else if (e.path.find("cameo") != std::string::npos || e.path.find("CAMEO") != std::string::npos) {
		pal = "cameo.pal";
	} else if (e.ext == "sha") {
		pal = "unittem.pal";
	}
	if (!load_palette_named(v, pal) && !v.palette_entries.empty()) {
		const Entry& first = v.entries[v.palette_entries.front()];
		Palette p;
		if (parse_palette(first.bytes, p)) {
			v.palette = p;
			v.palette_label = first.name;
		}
	}
}

void select_entry(Viewer& v, int index)
{
	v.selected = index;
	v.reset_preview();
	v.status.clear();
	if (index < 0)
		return;
	const Entry& e = v.entries[index];
	const std::string& ext = e.ext;
	auto span = e.bytes;

	if (ext == "shp" || ext == "sha" || ext == "mrf" || theater_for(ext)) {
		if (theater_for(ext) && span.size() >= 16 && std::memcmp(span.data(), "RIFF", 4) != 0) {
			uint32_t w = span[8] | (span[9] << 8), h = span[12] | (span[13] << 8);
			if (w == 60 && h == 30) {
				v.tmp = std::make_unique<TmpFile>();
				if (!TmpFile::parse(span, *v.tmp)) {
					v.tmp.reset();
					v.status = "not a valid terrain template";
				}
			}
		}
		if (!v.tmp) {
			v.shp = std::make_unique<ShpFile>();
			if (!ShpFile::parse(span, *v.shp)) {
				v.shp.reset();
				v.status = "not a valid SHP file";
			}
		}
		choose_default_palette(v, e);
	} else if (ext == "vxl") {
		v.vxl = std::make_unique<VxlFile>();
		v.zoom = 2;
		if (!VxlFile::parse(span, *v.vxl)) {
			v.vxl.reset();
			v.status = "not a valid VXL file";
		} else {
			// The animation file with the same name, if any.
			std::string hva_name = e.name.substr(0, e.name.size() - 3) + "hva";
			v.open_hold = v.vfs.open(hva_name);
			if (v.open_hold) {
				auto h = std::make_unique<HvaFile>();
				if (HvaFile::parse(v.open_hold.bytes, *h))
					v.hva = std::move(h);
			}
		}
		choose_default_palette(v, e);
	} else if (ext == "pal") {
		Palette p;
		if (parse_palette(span, p)) {
			v.palette = p;
			v.palette_label = e.name;
		} else {
			v.status = "not a valid palette";
		}
	} else if (ext == "pcx") {
		if (parse_pcx(span, v.pcx))
			v.pcx_ptr = &v.pcx;
		else
			v.status = "unsupported PCX";
	} else if (ext == "csf") {
		v.csf = std::make_unique<CsfFile>();
		if (!CsfFile::parse(span, *v.csf)) {
			v.csf.reset();
			v.status = "not a valid string table";
		}
	} else if (ext == "map" || ext == "yrm" || ext == "mpr" || ext == "mmx" || ext == "yro") {
		v.map = std::make_unique<MapData>();
		std::string error;
		if (!load_map(span, *v.map, &error)) {
			v.map.reset();
			v.status = "map error: " + error;
		}
	} else if (ext == "ini" || ext == "txt" || ext == "pkt" || ext == "map") {
		v.text.assign(reinterpret_cast<const char*>(span.data()), std::min<size_t>(span.size(), 200000));
	} else if (ext == "wav") {
		if (!decode_wav(span, v.audio))
			v.status = "cannot decode this sound";
	}
}

// ---- Rendering of previews into RGBA buffers ----

Rgb remapped(const Viewer& v, uint8_t index)
{
	Rgb c = v.palette.colors[index];
	if (v.house_color != 0 && index >= 16 && index <= 31) {
		const Rgb base = kHouseColors[v.house_color].base;
		// Keep the original brightness ramp, tint it with the house colour.
		const float lum = (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255.0f;
		const float t = 0.15f + 0.85f * lum;
		c = { uint8_t(std::min(255.0f, base.r * t)), uint8_t(std::min(255.0f, base.g * t)), uint8_t(std::min(255.0f, base.b * t)) };
	}
	return c;
}

void put_pixel(std::vector<uint8_t>& rgba, int w, int x, int y, Rgb c, uint8_t a = 255)
{
	uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
	p[0] = c.r;
	p[1] = c.g;
	p[2] = c.b;
	p[3] = a;
}

void render_shp(Viewer& v)
{
	const ShpFrame& f = v.shp->frames()[v.frame];
	v.tex_w = std::max<int>(1, v.shp->width());
	v.tex_h = std::max<int>(1, v.shp->height());
	v.rgba.assign(size_t(v.tex_w) * v.tex_h * 4, 0);
	std::vector<uint8_t> px;
	if (!v.shp->decode_frame(v.frame, px) || px.empty())
		return;
	for (int y = 0; y < f.height; ++y) {
		for (int x = 0; x < f.width; ++x) {
			const uint8_t idx = px[size_t(y) * f.width + x];
			if (idx != 0)
				put_pixel(v.rgba, v.tex_w, f.x + x, f.y + y, remapped(v, idx));
		}
	}
}

void render_tmp(Viewer& v)
{
	// Place every tile at its stored position, extra pixels included.
	int min_x = 0, min_y = 0, max_x = 1, max_y = 1;
	bool first = true;
	auto grow = [&](int x0, int y0, int x1, int y1) {
		if (first) {
			min_x = x0; min_y = y0; max_x = x1; max_y = y1;
			first = false;
		} else {
			min_x = std::min(min_x, x0); min_y = std::min(min_y, y0);
			max_x = std::max(max_x, x1); max_y = std::max(max_y, y1);
		}
	};
	for (const TmpTile& t : v.tmp->tiles()) {
		if (!t.present)
			continue;
		grow(t.x, t.y, t.x + kTileWidth, t.y + kTileHeight);
		if (t.has_extra())
			grow(t.extra_x, t.extra_y, t.extra_x + int(t.extra_width), t.extra_y + int(t.extra_height));
	}
	v.tex_w = max_x - min_x;
	v.tex_h = max_y - min_y;
	v.rgba.assign(size_t(v.tex_w) * v.tex_h * 4, 0);
	std::vector<uint8_t> diamond;
	for (const TmpTile& t : v.tmp->tiles()) {
		if (!t.present)
			continue;
		if (t.has_extra()) {
			for (uint32_t y = 0; y < t.extra_height; ++y) {
				for (uint32_t x = 0; x < t.extra_width; ++x) {
					const uint8_t idx = t.extra[y * t.extra_width + x];
					if (idx)
						put_pixel(v.rgba, v.tex_w, t.extra_x - min_x + int(x), t.extra_y - min_y + int(y), v.palette.colors[idx]);
				}
			}
		}
		expand_tile_diamond(t.image, diamond);
		for (int y = 0; y < kTileHeight; ++y) {
			for (int x = 0; x < kTileWidth; ++x) {
				const uint8_t idx = diamond[size_t(y) * kTileWidth + x];
				const int w = tile_row_width(y);
				const int x0 = (kTileWidth - w) / 2;
				if (x >= x0 && x < x0 + w)
					put_pixel(v.rgba, v.tex_w, t.x - min_x + x, t.y - min_y + y, v.palette.colors[idx]);
			}
		}
	}
}

void render_vxl(Viewer& v)
{
	constexpr int kSize = 300;
	v.tex_w = v.tex_h = kSize;
	v.rgba.assign(size_t(kSize) * kSize * 4, 0);
	std::vector<float> depth(size_t(kSize) * kSize, -1e30f);

	const float yaw = float(v.voxel_yaw) * 3.14159265f / 180.0f;
	const float pitch = float(v.voxel_pitch) * 3.14159265f / 180.0f;
	const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);

	struct Pt { float x, y, z; uint8_t color; };
	std::vector<Pt> points;
	std::vector<Voxel> voxels;
	float extent = 1e-6f;
	float voxel_world = 0.0f; // size of one voxel in world units
	for (size_t li = 0; li < v.vxl->limbs().size(); ++li) {
		const VxlLimb& limb = v.vxl->limbs()[li];
		if (!v.vxl->decode_limb(li, voxels))
			continue;
		const float* m = limb.transform;
		if (v.hva && li < v.hva->section_count())
			m = v.hva->matrix(uint32_t(v.hva_frame) % v.hva->frame_count(), uint32_t(li));
		const float vs[3] = { (limb.max[0] - limb.min[0]) / limb.xsize, (limb.max[1] - limb.min[1]) / limb.ysize, (limb.max[2] - limb.min[2]) / limb.zsize };
		voxel_world = std::max(voxel_world, limb.scale * std::max({ vs[0], vs[1], vs[2] }));
		for (const Voxel& vox : voxels) {
			const float p[3] = { limb.min[0] + (vox.x + 0.5f) * vs[0], limb.min[1] + (vox.y + 0.5f) * vs[1], limb.min[2] + (vox.z + 0.5f) * vs[2] };
			float w[3];
			for (int r = 0; r < 3; ++r)
				w[r] = limb.scale * (m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3]);
			points.push_back({ w[0], w[1], w[2], vox.color });
			extent = std::max({ extent, std::fabs(w[0]), std::fabs(w[1]), std::fabs(w[2]) });
		}
	}
	// The game's models face +x with z up. Orbit around the vertical axis, tilt by pitch.
	const float scale = (kSize * 0.45f) / extent;
	const int splat = std::max(1, int(voxel_world * scale + 0.5f));
	for (const Pt& p : points) {
		const float x1 = p.x * cy - p.y * sy, y1 = p.x * sy + p.y * cy;
		const float sx = x1, sz = p.z * cp - y1 * sp, depth_v = p.z * sp + y1 * cp;
		const int px = int(kSize / 2 + sx * scale), py = int(kSize / 2 + kSize * 0.1f - sz * scale);
		for (int dy = 0; dy < splat; ++dy) {
			for (int dx = 0; dx < splat; ++dx) {
				const int x = px + dx - splat / 2, y = py + dy - splat / 2;
				if (x < 0 || y < 0 || x >= kSize || y >= kSize)
					continue;
				float& d = depth[size_t(y) * kSize + x];
				if (depth_v > d) {
					d = depth_v;
					put_pixel(v.rgba, kSize, x, y, remapped(v, p.color));
				}
			}
		}
	}
}

void render_pcx(Viewer& v)
{
	const PcxImage& img = *v.pcx_ptr;
	v.tex_w = int(img.width);
	v.tex_h = int(img.height);
	v.rgba.assign(size_t(v.tex_w) * v.tex_h * 4, 255);
	for (int y = 0; y < v.tex_h; ++y) {
		for (int x = 0; x < v.tex_w; ++x) {
			Rgb c;
			if (img.truecolor) {
				const uint8_t* p = &img.rgb[(size_t(y) * img.width + x) * 3];
				c = { p[0], p[1], p[2] };
			} else {
				c = img.palette[img.pixels[size_t(y) * img.width + x]];
			}
			put_pixel(v.rgba, v.tex_w, x, y, c);
		}
	}
}

void render_palette(Viewer& v)
{
	v.tex_w = v.tex_h = 16;
	v.rgba.assign(16 * 16 * 4, 255);
	for (int i = 0; i < 256; ++i)
		put_pixel(v.rgba, 16, i % 16, i / 16, v.palette.colors[i]);
}

void render_map_preview(Viewer& v)
{
	const MapPreviewImage& p = v.map->preview;
	v.tex_w = int(p.width);
	v.tex_h = int(p.height);
	v.rgba.assign(size_t(v.tex_w) * v.tex_h * 4, 255);
	for (int i = 0; i < v.tex_w * v.tex_h; ++i)
		put_pixel(v.rgba, v.tex_w, i % v.tex_w, i / v.tex_w, { p.rgb[i * 3], p.rgb[i * 3 + 1], p.rgb[i * 3 + 2] });
}

void refresh_texture(Viewer& v)
{
	if (!v.dirty)
		return;
	v.dirty = false;
	v.rgba.clear();
	if (v.shp && !v.shp->frames().empty())
		render_shp(v);
	else if (v.tmp)
		render_tmp(v);
	else if (v.vxl)
		render_vxl(v);
	else if (v.pcx_ptr)
		render_pcx(v);
	else if (v.map && v.map->preview.width)
		render_map_preview(v);
	else if (!v.entries.empty() && v.selected >= 0 && v.entries[v.selected].ext == "pal")
		render_palette(v);
	if (!v.rgba.empty())
		v.texture.upload(v.tex_w, v.tex_h, v.rgba.data());
}

// ---- UI ----

void draw_palette_controls(Viewer& v)
{
	if (ImGui::BeginCombo("Palette", v.palette_label.c_str())) {
		for (int idx : v.palette_entries) {
			const Entry& pe = v.entries[idx];
			const bool is_sel = pe.name == v.palette_label;
			if (ImGui::Selectable((pe.name + "##" + std::to_string(idx)).c_str(), is_sel)) {
				Palette p;
				if (parse_palette(pe.bytes, p)) {
					v.palette = p;
					v.palette_label = pe.name;
					v.dirty = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginCombo("House colour", kHouseColors[v.house_color].name)) {
		for (int i = 0; i < int(std::size(kHouseColors)); ++i) {
			if (ImGui::Selectable(kHouseColors[i].name, i == v.house_color)) {
				v.house_color = i;
				v.dirty = true;
			}
		}
		ImGui::EndCombo();
	}
}

void draw_image(Viewer& v, bool checker = true)
{
	if (!v.texture.valid())
		return;
	ImGui::SliderInt("Zoom", &v.zoom, 1, 8);
	const ImVec2 size(float(v.texture.width() * v.zoom), float(v.texture.height() * v.zoom));
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	if (checker) {
		dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(48, 52, 58, 255));
		const float cell = 8.0f * 2;
		for (float y = 0; y < size.y; y += cell) {
			for (float x = 0; x < size.x; x += cell) {
				if ((int(x / cell) + int(y / cell)) % 2 == 0)
					dl->AddRectFilled(ImVec2(pos.x + x, pos.y + y), ImVec2(std::min(pos.x + x + cell, pos.x + size.x), std::min(pos.y + y + cell, pos.y + size.y)), IM_COL32(60, 64, 70, 255));
			}
		}
	}
	ImGui::Image(static_cast<ImTextureID>(v.texture.id()), size);
}

void draw_preview(Viewer& v)
{
	if (v.selected < 0) {
		ImGui::TextWrapped("Pick a file on the left.");
		return;
	}
	const Entry& e = v.entries[v.selected];
	ImGui::Text("%s", e.path.c_str());
	ImGui::Text("%u bytes%s", e.size, e.named ? "" : "  (name unknown)");
	ImGui::Separator();
	if (!v.status.empty())
		ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", v.status.c_str());

	if (v.shp) {
		const int n = int(v.shp->frames().size());
		ImGui::Text("SHP: %dx%d, %d frames", v.shp->width(), v.shp->height(), n);
		if (ImGui::SliderInt("Frame", &v.frame, 0, n - 1))
			v.dirty = true;
		draw_palette_controls(v);
		refresh_texture(v);
		draw_image(v);
	} else if (v.tmp) {
		ImGui::Text("Terrain template: %ux%u slots, %zu tiles", v.tmp->blocks_wide(), v.tmp->blocks_high(), v.tmp->present_count());
		draw_palette_controls(v);
		refresh_texture(v);
		draw_image(v);
	} else if (v.vxl) {
		ImGui::Text("Voxel model: %zu limbs%s", v.vxl->limbs().size(), v.hva ? ", with animation" : "");
		for (const VxlLimb& l : v.vxl->limbs())
			ImGui::BulletText("%s  %ux%ux%u, normals %u", l.name.c_str(), l.xsize, l.ysize, l.zsize, l.normal_mode);
		bool changed = ImGui::SliderInt("Turn", &v.voxel_yaw, 0, 359);
		changed |= ImGui::SliderInt("Tilt", &v.voxel_pitch, -80, 80);
		if (v.hva)
			changed |= ImGui::SliderInt("Animation frame", &v.hva_frame, 0, int(v.hva->frame_count()) - 1);
		if (changed)
			v.dirty = true;
		draw_palette_controls(v);
		refresh_texture(v);
		draw_image(v, false);
	} else if (v.pcx_ptr) {
		ImGui::Text("PCX: %ux%u %s", v.pcx.width, v.pcx.height, v.pcx.truecolor ? "24-bit" : "256 colours");
		refresh_texture(v);
		draw_image(v, false);
	} else if (e.ext == "pal") {
		ImGui::Text("Palette: 256 colours (shown 16 x 16)");
		v.zoom = std::max(v.zoom, 12);
		refresh_texture(v);
		draw_image(v, false);
	} else if (v.csf) {
		ImGui::Text("String table: %zu labels, language %u", v.csf->label_count(), v.csf->language());
		ImGui::InputText("Label", v.csf_label, sizeof v.csf_label);
		if (const CsfString* s = v.csf->find(v.csf_label))
			ImGui::TextWrapped("%s", utf16_to_utf8(s->text).c_str());
		else if (v.csf_label[0])
			ImGui::TextDisabled("(no such label)");
	} else if (v.map) {
		const MapData& m = *v.map;
		ImGui::Text("Map \"%s\"  theater %s  size %dx%d  playable %dx%d", m.ini.get_string("Basic", "Name").c_str(), m.theater.c_str(), m.map_width, m.map_height, m.local_width, m.local_height);
		ImGui::Text("%zu terrain cells, %zu overlay bytes, %zu sections", m.cells.size(), m.overlay.size(), m.ini.sections().size());
		refresh_texture(v);
		draw_image(v, false);
	} else if (!v.text.empty()) {
		ImGui::BeginChild("text", ImVec2(0, 0), ImGuiChildFlags_Borders);
		ImGui::TextUnformatted(v.text.c_str());
		ImGui::EndChild();
	} else if (!v.audio.samples.empty()) {
		const double seconds = double(v.audio.samples.size()) / v.audio.channels / v.audio.sample_rate;
		ImGui::Text("Sound: %u Hz, %u channel(s), %.2f s", v.audio.sample_rate, v.audio.channels, seconds);
		if (ImGui::Button("Play") && v.audio_stream) {
			SDL_AudioSpec spec { SDL_AUDIO_S16, v.audio.channels, int(v.audio.sample_rate) };
			SDL_SetAudioStreamFormat(v.audio_stream, &spec, nullptr);
			SDL_ClearAudioStream(v.audio_stream);
			SDL_PutAudioStreamData(v.audio_stream, v.audio.samples.data(), int(v.audio.samples.size() * sizeof(int16_t)));
			SDL_ResumeAudioStreamDevice(v.audio_stream);
		}
	} else {
		// Anything else: a hex dump of the start.
		std::string dump;
		char line[96];
		const size_t n = std::min<size_t>(e.bytes.size(), 512);
		for (size_t i = 0; i < n; i += 16) {
			int len = std::snprintf(line, sizeof line, "%06zx  ", i);
			for (size_t k = 0; k < 16 && i + k < n; ++k)
				len += std::snprintf(line + len, sizeof line - len, "%02x ", e.bytes[i + k]);
			dump += line;
			dump += '\n';
		}
		ImGui::TextUnformatted(dump.c_str());
	}
}

struct App {
	SDL_Window* window = nullptr;
	SDL_GLContext gl = nullptr;
	Viewer viewer;
	bool gles = false;
};

using GlClearColorFn = void (*)(float, float, float, float);
using GlClearFn = void (*)(unsigned int);
using GlViewportFn = void (*)(int, int, int, int);
GlClearColorFn gl_clear_color = nullptr;
GlClearFn gl_clear = nullptr;
GlViewportFn gl_viewport = nullptr;

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
	std::string game_dir = std::getenv("RA_GAME_DIR") ? std::getenv("RA_GAME_DIR") : "";
	std::string names_path = std::getenv("RA_NAMES_FILE") ? std::getenv("RA_NAMES_FILE") : "";
	std::string select, palette_name;
	vfs::GameKind kind = vfs::GameKind::YurisRevenge;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--ra2")
			kind = vfs::GameKind::RedAlert2;
		else if (a == "--names" && i + 1 < argc)
			names_path = argv[++i];
		else if (a == "--palette" && i + 1 < argc)
			palette_name = argv[++i];
		else if (a == "--select" && i + 1 < argc)
			select = lower(argv[++i]); // open the first file whose path ends with this at start-up
		else
			game_dir = a;
	}
	if (game_dir.empty()) {
		std::fprintf(stderr, "usage: asset_viewer [--ra2] [--names names.txt] <game folder>\n");
		return SDL_APP_FAILURE;
	}

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
		return SDL_APP_FAILURE;
	auto* app = new App();
	*appstate = app;

	Viewer& v = app->viewer;
	v.vfs.set_loose_directory(game_dir);
	const auto mounted = v.vfs.mount_standard(kind);
	std::printf("Mounted %zu archives\n", mounted.size());
	build_entries(v, names_path);
	apply_filter(v);
	std::printf("%zu files\n", v.entries.size());
	if (!select.empty()) {
		for (size_t i = 0; i < v.entries.size(); ++i) {
			const std::string p = lower(v.entries[i].path);
			if (p.size() >= select.size() && p.compare(p.size() - select.size(), select.size(), select) == 0) {
				select_entry(v, static_cast<int>(i));
				if (!palette_name.empty())
					load_palette_named(v, palette_name);
				break;
			}
		}
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	app->window = SDL_CreateWindow("Asset viewer", 1400, 860, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!app->window)
		return SDL_APP_FAILURE;
	tools::place_window_on_display(app->window);
	struct Attempt { int profile, major, minor; bool gles; };
	for (const Attempt& a : { Attempt { SDL_GL_CONTEXT_PROFILE_ES, 3, 0, true }, Attempt { SDL_GL_CONTEXT_PROFILE_CORE, 3, 3, false } }) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, a.profile);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
		app->gl = SDL_GL_CreateContext(app->window);
		if (app->gl) {
			app->gles = a.gles;
			break;
		}
	}
	if (!app->gl)
		return SDL_APP_FAILURE;
	SDL_GL_MakeCurrent(app->window, app->gl);
	SDL_GL_SetSwapInterval(1);
	gl_clear_color = reinterpret_cast<GlClearColorFn>(SDL_GL_GetProcAddress("glClearColor"));
	gl_clear = reinterpret_cast<GlClearFn>(SDL_GL_GetProcAddress("glClear"));
	gl_viewport = reinterpret_cast<GlViewportFn>(SDL_GL_GetProcAddress("glViewport"));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	if (scale > 0.0f) {
		ImGui::GetStyle().ScaleAllSizes(scale);
		ImGui::GetStyle().FontScaleDpi = scale;
	}
	ImGui_ImplSDL3_InitForOpenGL(app->window, app->gl);
	ImGui_ImplOpenGL3_Init(app->gles ? "#version 300 es" : "#version 330 core");

	SDL_AudioSpec spec { SDL_AUDIO_S16, 2, 22050 };
	v.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	(void)appstate;
	ImGui_ImplSDL3_ProcessEvent(event);
	if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
		return SDL_APP_SUCCESS;
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	auto* app = static_cast<App*>(appstate);
	Viewer& v = app->viewer;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const float left_width = std::min(vp->Size.x * 0.4f, 520.0f * ImGui::GetStyle().FontScaleDpi);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;

	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(ImVec2(left_width, vp->WorkSize.y));
	ImGui::Begin("Files", nullptr, flags);
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##filter", "filter (part of a name or path)", v.filter, sizeof v.filter);
	if (v.last_filter != v.filter)
		apply_filter(v);
	ImGui::Text("%zu of %zu files", v.filtered.size(), v.entries.size());
	ImGui::BeginChild("list");
	ImGuiListClipper clipper;
	clipper.Begin(int(v.filtered.size()));
	while (clipper.Step()) {
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
			const int idx = v.filtered[row];
			const Entry& e = v.entries[idx];
			if (ImGui::Selectable((e.path + "##" + std::to_string(idx)).c_str(), idx == v.selected))
				select_entry(v, idx);
		}
	}
	ImGui::EndChild();
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + left_width, vp->WorkPos.y));
	ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - left_width, vp->WorkSize.y));
	ImGui::Begin("Preview", nullptr, flags);
	draw_preview(v);
	ImGui::End();

	ImGui::Render();
	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(app->window, &w, &h);
	gl_viewport(0, 0, w, h);
	gl_clear_color(0.08f, 0.09f, 0.11f, 1.0f);
	gl_clear(0x00004000);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(app->window);
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult)
{
	auto* app = static_cast<App*>(appstate);
	if (!app)
		return;
	app->viewer.texture.release();
	if (ImGui::GetCurrentContext()) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
	if (app->gl)
		SDL_GL_DestroyContext(app->gl);
	if (app->window)
		SDL_DestroyWindow(app->window);
	delete app;
}
