#include "render/map_renderer.h"

#include "core/dir.h"
#include "core/log.h"
#include "formats/palette.h"
#include "formats/tmp.h"
#include "sim/world/land.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ra::render {

namespace {

enum class PaletteSlot { Tile, Object, Unit };

struct DrawCommand {
	AtlasRect rect;
	float x, y;
	Tint tint;
	PaletteSlot palette;
	int house = -1; // with PaletteSlot::Unit: which house palette (-1 = the plain unit palette)
};

// Pixels of a 60 x 30 diamond that its row widths cover.
void diamond_coverage(std::vector<uint8_t>& coverage)
{
	using namespace ra::formats;
	coverage.assign(size_t(kTileWidth) * kTileHeight, 0);
	for (int y = 0; y < kTileHeight; ++y) {
		const int w = tile_row_width(y);
		const int x0 = (kTileWidth - w) / 2;
		for (int x = 0; x < w; ++x)
			coverage[size_t(y) * kTileWidth + x0 + x] = 255;
	}
}

// Moves the top-left of the diamond area into place: draws an image (indices, coverage and
// depth of one tile) into diamond layout.
void expand_plane(std::span<const uint8_t> packed, std::vector<uint8_t>& out)
{
	ra::formats::expand_tile_diamond(packed, out);
}

struct Hsv {
	int h = 0, s = 0, v = 0; // 0..255 each, as the rules write colours
};

ra::formats::Rgb hsv_to_rgb(float h, float s, float v)
{
	const float h6 = (h - std::floor(h)) * 6.0f;
	const int i = int(h6);
	const float f = h6 - float(i);
	const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
	float r, g, b;
	switch (i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
	auto to8 = [](float c) { return uint8_t(std::clamp(int(c * 255.0f + 0.5f), 0, 255)); };
	return { to8(r), to8(g), to8(b) };
}

// The letter that stands for a theater in the file names of buildings drawn with NewTheater=yes.
char theater_letter(const std::string& extension)
{
	if (extension == "tem") return 'T';
	if (extension == "sno") return 'A';
	if (extension == "urb") return 'U';
	if (extension == "des") return 'D';
	if (extension == "lun") return 'L';
	if (extension == "ubn") return 'N';
	return 'T';
}

} // namespace

bool MapRenderer::init()
{
	if (!batch_.init())
		return false;
	return true;
}

bool MapRenderer::set_world(const ra::world::World* world, const ra::vfs::Vfs* vfs)
{
	world_ = world;
	vfs_ = vfs;
	tile_cache_.clear();
	overlay_sprites_.clear();
	terrain_sprites_.clear();
	object_sprites_.clear();
	house_palettes_.clear();
	house_palette_index_.clear();
	if (!world || !vfs)
		return false;

	auto load = [&](const char* name, PaletteTexture& out) {
		const auto data = vfs->open(name);
		ra::formats::Palette p;
		if (!data || !ra::formats::parse_palette(data.bytes, p)) {
			RA_LOG_WARN("palette %s not found or invalid", name);
			return false;
		}
		out.set(p);
		return true;
	};
	bool ok = load(world->theater->tile_palette, tile_palette_);
	ok &= load(world->theater->object_palette, object_palette_);
	ok &= load(world->theater->unit_palette, unit_palette_);
	if (const auto data = vfs->open(world->theater->unit_palette))
		ra::formats::parse_palette(data.bytes, unit_colors_);
	return ok;
}

MapRenderer::Bounds MapRenderer::playable_bounds() const
{
	Bounds b { 0, 0, 1, 1 };
	if (!world_)
		return b;
	const auto& m = world_->map;
	// The map diamond spans x in (-W * 30, W * 30) and y in (W * 15, (W + 2H) * 15). LocalSize is a
	// rectangle inside it in units of whole tiles: 60 pixels per column and 30 per row, from the
	// diamond's top-left corner.
	b.left = float(-m.map_width * 30 + m.local_x * 60);
	b.top = float(m.map_width * 15 + m.local_y * 30);
	b.right = b.left + float(m.local_width * 60);
	b.bottom = b.top + float(m.local_height * 30);
	return b;
}

Tint MapRenderer::tint_for_level(int level) const
{
	if (!use_lighting || !world_)
		return {};
	const auto& l = world_->lighting;
	const float intensity = float(l.cell_intensity(level)) / 1000.0f;
	return { float(l.red_1000()) / 1000.0f * intensity, float(l.green_1000()) / 1000.0f * intensity, float(l.blue_1000()) / 1000.0f * intensity };
}

const MapRenderer::TileImages& MapRenderer::tile_images(int tile, int sub_tile)
{
	const uint64_t key = (uint64_t(uint32_t(tile)) << 8) | uint8_t(sub_tile);
	TileImages& images = tile_cache_[key];
	if (images.built)
		return images;
	images.built = true;
	const ra::formats::TmpTile* t = world_->tiles->tile(tile, sub_tile);
	if (!t)
		return images;

	std::vector<uint8_t> indices, depth, coverage;
	diamond_coverage(coverage);
	expand_plane(t->image, indices);
	if (!t->depth.empty())
		expand_plane(t->depth, depth);
	images.diamond = atlas_.add(ra::formats::kTileWidth, ra::formats::kTileHeight, indices.data(), coverage.data(), depth.empty() ? nullptr : depth.data());

	if (t->has_extra() && t->extra_width > 0 && t->extra_height > 0) {
		images.extra = atlas_.add(int(t->extra_width), int(t->extra_height), t->extra.data(), nullptr, t->extra_depth.empty() ? nullptr : t->extra_depth.data());
		images.extra_dx = t->extra_x - t->x;
		images.extra_dy = t->extra_y - t->y;
	}
	return images;
}

MapRenderer::SpriteSet& MapRenderer::sprite_set(std::unordered_map<std::string, SpriteSet>& cache, const std::string& image)
{
	SpriteSet& set = cache[image];
	if (set.loaded)
		return set;
	set.loaded = true;

	// An art entry with Theater=yes has one file per theater: name.tem, name.sno...; others are name.shp.
	const ra::rules::RuleObject art = world_->rules.art_section(image);
	const bool theater_specific = art.get_bool("Theater");
	const std::string file = image + "." + (theater_specific ? world_->theater->extension : "shp");
	set.data = vfs_->open(file);
	if (set.data && ra::formats::ShpFile::parse(set.data.bytes, set.shp)) {
		set.ok = true;
		set.use_theater_palette = theater_specific;
		set.frames.assign(set.shp.frames().size(), AtlasRect {});
		set.frame_built.assign(set.shp.frames().size(), 0);
	}
	return set;
}

const AtlasRect* MapRenderer::sprite_frame(SpriteSet& set, int frame)
{
	if (!set.ok || frame < 0 || frame >= static_cast<int>(set.frames.size()))
		return nullptr;
	if (!set.frame_built[static_cast<size_t>(frame)]) {
		set.frame_built[static_cast<size_t>(frame)] = 1;
		std::vector<uint8_t> pixels;
		const auto& f = set.shp.frames()[static_cast<size_t>(frame)];
		if (set.shp.decode_frame(static_cast<size_t>(frame), pixels) && !pixels.empty())
			set.frames[static_cast<size_t>(frame)] = atlas_.add(f.width, f.height, pixels.data());
	}
	const AtlasRect& r = set.frames[static_cast<size_t>(frame)];
	return r.valid() ? &r : nullptr;
}

MapRenderer::SpriteSet& MapRenderer::object_sprite_set(const std::string& image, bool new_theater)
{
	SpriteSet& set = object_sprites_[image];
	if (set.loaded)
		return set;
	set.loaded = true;
	// Buildings with NewTheater=yes have a file per theater, named with the theater's letter in
	// second place (GACNST is gtcnst.shp in the temperate theater); others are image.shp.
	std::vector<std::string> candidates;
	if (new_theater && image.size() > 2) {
		std::string themed = image;
		themed[1] = theater_letter(world_->theater->extension);
		candidates.push_back(themed + ".shp");
		candidates.push_back(image + "." + world_->theater->extension);
	}
	candidates.push_back(image + ".shp");
	for (const std::string& file : candidates) {
		set.data = vfs_->open(file);
		if (set.data && ra::formats::ShpFile::parse(set.data.bytes, set.shp)) {
			set.ok = true;
			set.frames.assign(set.shp.frames().size(), AtlasRect {});
			set.frame_built.assign(set.shp.frames().size(), 0);
			break;
		}
	}
	return set;
}

int MapRenderer::house_palette(const std::string& owner)
{
	const std::string key = ra::ini::to_lower(owner);
	if (auto it = house_palette_index_.find(key); it != house_palette_index_.end())
		return it->second;

	// The house's colour name: the map's own house section first, then the country's rules.
	const ra::world::World& w = *world_;
	std::string colour = w.map.ini.get_string(owner, "Color");
	if (colour.empty())
		colour = w.rules.rules().get_string(owner, "Color");
	int index = -1;
	Hsv hsv;
	const std::string* spec = colour.empty() ? nullptr : w.rules.rules().get("Colors", colour);
	if (spec) {
		const auto parts = ra::ini::IniFile::split_list(*spec);
		if (parts.size() >= 3 && ra::ini::parse_int(parts[0], hsv.h) && ra::ini::parse_int(parts[1], hsv.s) && ra::ini::parse_int(parts[2], hsv.v)) {
			// The remap range (palette entries 16..31) is a ramp from bright to dark. Keep each entry's
			// brightness relative to the brightest, and give the ramp the colour's hue, saturation and value.
			int brightest = 1;
			for (int i = 16; i <= 31; ++i) {
				const auto c = unit_colors_.colors[static_cast<size_t>(i)];
				brightest = std::max({ brightest, int(c.r), int(c.g), int(c.b) });
			}
			ra::formats::Palette p = unit_colors_;
			for (int i = 16; i <= 31; ++i) {
				const auto c = unit_colors_.colors[static_cast<size_t>(i)];
				const float rel = float(std::max({ int(c.r), int(c.g), int(c.b) })) / float(brightest);
				p.colors[static_cast<size_t>(i)] = hsv_to_rgb(float(hsv.h) / 255.0f, float(hsv.s) / 255.0f, float(hsv.v) / 255.0f * rel);
			}
			auto tex = std::make_unique<PaletteTexture>();
			tex->set(p);
			house_palettes_.push_back(std::move(tex));
			index = static_cast<int>(house_palettes_.size()) - 1;
		}
	}
	house_palette_index_[key] = index;
	return index;
}

void MapRenderer::draw(const View& requested_view)
{
	stats_ = {};
	// Snap the camera to whole screen pixels. A fractional scroll puts tile edges between pixels,
	// and the nearest-texel lookup then skips rows of the tile images, leaving hairline gaps.
	View view = requested_view;
	view.scroll_x = std::floor(view.scroll_x * view.zoom + 0.5f) / view.zoom;
	view.scroll_y = std::floor(view.scroll_y * view.zoom + 0.5f) / view.zoom;
	if (!world_ || !vfs_)
		return;
	using ra::world::MapGrid;
	const ra::world::World& w = *world_;
	const MapGrid& grid = w.grid;

	// The part of the world visible on screen, with margins for tall things (cliffs, trees).
	const float left = view.scroll_x;
	const float top = view.scroll_y;
	const float right = view.scroll_x + float(view.width) / view.zoom;
	const float bottom = view.scroll_y + float(view.height) / view.zoom;
	const int u_min = std::max(0, int(std::floor((top - 260.0f) / 15.0f)));
	const int u_max = std::min(2 * MapGrid::kSize - 2, int(std::ceil((bottom + 16.0f * 15.0f + 40.0f) / 15.0f)));
	const int v_min = int(std::floor((left - 150.0f) / 30.0f));
	const int v_max = int(std::ceil((right + 90.0f) / 30.0f));

	std::vector<DrawCommand> commands;
	commands.reserve(8192);

	auto each_visible_cell = [&](auto&& fn) {
		for (int u = u_min; u <= u_max; ++u) {
			for (int v = v_min; v <= v_max; ++v) {
				if (((u + v) & 1) != 0)
					continue;
				const int x = (u + v) / 2, y = (u - v) / 2;
				if (!MapGrid::in_grid(x, y))
					continue;
				const ra::world::MapCell& c = grid.at(x, y);
				if (c.exists)
					fn(x, y, c);
			}
		}
	};

	// 1. Terrain tiles, with their overhanging parts.
	if (draw_tiles) {
		each_visible_cell([&](int x, int y, const ra::world::MapCell& c) {
			if (c.tile < 0)
				return;
			const TileImages& images = tile_images(c.tile, c.sub_tile);
			if (!images.diamond.valid())
				return;
			const float dx = float((x - y) * 30 - 30);
			const float dy = float((x + y) * 15 - c.level * ra::world::kPixelsPerLevel);
			const Tint tint = tint_for_level(c.level);
			commands.push_back({ images.diamond, dx, dy, tint, PaletteSlot::Tile });
			if (images.extra.valid())
				commands.push_back({ images.extra, dx + float(images.extra_dx), dy + float(images.extra_dy), tint, PaletteSlot::Tile });
			++stats_.tiles_drawn;
		});
	}

	// Adds a sprite frame centred on a point: the canvas is centred and the frame sits at its own offset inside it.
	auto draw_sprite_at = [&](SpriteSet& set, int frame, float cx, float cy, Tint tint, std::vector<DrawCommand>& out, PaletteSlot palette = PaletteSlot::Tile) {
		const AtlasRect* r = sprite_frame(set, frame);
		if (!r)
			return;
		const auto& f = set.shp.frames()[static_cast<size_t>(frame)];
		const float x = cx - float(set.shp.width() / 2) + float(f.x);
		const float y = cy - float(set.shp.height() / 2) + float(f.y);
		// Overlays, walls, bridges and terrain objects use the theater's iso palette (checked by eye:
		// trees and bridges come out garbled with temperat.pal or the unit palette); ore passes its own.
		out.push_back({ *r, x, y, tint, palette });
	};

	// The centre of a cell on screen, at the height of the ground there.
	auto cell_center = [&](int x, int y, const ra::world::MapCell& c) {
		const int z = ra::world::floor_height(c.level, c.slope, 128, 128);
		return std::pair<float, float>(float((x - y) * 30), float((x + y) * 15 + 15 - ra::world::height_to_pixels(z)));
	};

	// 2. Overlays: ore, walls, bridges.
	if (draw_overlays) {
		each_visible_cell([&](int x, int y, const ra::world::MapCell& c) {
			if (c.overlay < 0 || c.overlay >= static_cast<int>(w.overlays.size()))
				return;
			const ra::world::OverlayInfo& info = w.overlays[static_cast<size_t>(c.overlay)];
			SpriteSet& set = sprite_set(overlay_sprites_, info.image);
			if (!set.ok)
				return;
			auto [cx, cy] = cell_center(x, y, c);
			// Walls, ore, crates and veins are drawn 12 pixels higher (OverlayClass::DrawAgain).
			if (info.wall || info.tiberium || info.crate || info.veins)
				cy -= 12.0f;
			int frame = c.overlay_data;
			if (frame >= static_cast<int>(set.frames.size()))
				frame = static_cast<int>(set.frames.size()) - 1;
			// Ore and gems use the theater palette (temperat.pal...), not the iso palette; they are always drawn at full brightness.
			draw_sprite_at(set, frame, cx, cy, info.tiberium ? Tint {} : tint_for_level(c.level), commands, info.tiberium ? PaletteSlot::Object : PaletteSlot::Tile);
		});
	}
	// 3. Terrain objects: trees and rocks, sorted into cell order.
	if (draw_terrain_objects) {
		std::vector<const ra::world::TerrainObject*> visible;
		for (const auto& t : w.terrain_objects) {
			const int u = t.x + t.y, v = t.x - t.y;
			if (u >= u_min && u <= u_max && v >= v_min && v <= v_max)
				visible.push_back(&t);
		}
		std::stable_sort(visible.begin(), visible.end(), [](const auto* a, const auto* b) { return a->x + a->y < b->x + b->y; });
		for (const auto* t : visible) {
			if (!MapGrid::in_grid(t->x, t->y))
				continue;
			const ra::world::MapCell& c = grid.at(t->x, t->y);
			const ra::rules::RuleObject type = w.rules.object(ra::rules::TypeKind::Terrain, t->type);
			SpriteSet& set = sprite_set(terrain_sprites_, w.rules.image_name(type));
			if (!set.ok)
				continue;
			auto [cx, cy] = cell_center(t->x, t->y, c);
			const Tint tint = tint_for_level(c.level);
			// The second half of the frames are the shadows.
			const int half = static_cast<int>(set.frames.size()) / 2;
			if (half > 0)
				draw_sprite_at(set, half, cx, cy, Tint { -1.0f, 0.0f, 0.0f }, commands);
			draw_sprite_at(set, 0, cx, cy, tint, commands);
		}
	}

	// 4. The objects the map starts with. Drawn in cell order so nearer ones cover farther ones.
	if (draw_objects) {
		struct Item {
			const ra::world::PlacedObject* obj;
			int key;
		};
		std::vector<Item> items;
		for (const auto& o : w.placed_objects) {
			const int u = o.x + o.y, v = o.x - o.y;
			if (u >= u_min - 8 && u <= u_max + 8 && v >= v_min - 8 && v <= v_max + 8)
				items.push_back({ &o, u });
		}
		std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
		using ra::rules::TypeKind;
		for (const Item& item : items) {
			const ra::world::PlacedObject& o = *item.obj;
			const ra::world::MapCell& c = grid.at(o.x, o.y);
			const ra::rules::RuleObject type = w.rules.object(o.kind, o.type);
			const std::string image = w.rules.image_name(type);
			const ra::rules::RuleObject art = w.rules.art_section(image);
			const int house = house_palette(o.owner);
			const Tint tint = tint_for_level(c.level);
			auto [cx, cy] = cell_center(o.x, o.y, c);

			auto add = [&](SpriteSet& set, int frame, float px, float py, Tint t) {
				const AtlasRect* r = sprite_frame(set, frame);
				if (!r)
					return;
				const auto& f = set.shp.frames()[static_cast<size_t>(frame)];
				commands.push_back({ *r, px - float(set.shp.width() / 2) + float(f.x), py - float(set.shp.height() / 2) + float(f.y), t, PaletteSlot::Unit, house });
			};

			if (o.kind == TypeKind::Building) {
				SpriteSet& set = object_sprite_set(image, art.get_bool("NewTheater"));
				if (!set.ok)
					continue;
				int fw = 1, fh = 1;
				const std::string foundation = art.get_string("Foundation");
				if (!foundation.empty())
					std::sscanf(foundation.c_str(), "%dx%d", &fw, &fh);
				// The foundation's centre; cell_center() is the centre of one cell.
				const float bx = cx + float((fw - fh) * 15);
				const float by = cy + float((fw + fh - 2) * 15) / 2.0f;
				add(set, 0, bx, by, tint);
			} else if (o.kind == TypeKind::Infantry) {
				SpriteSet& set = object_sprite_set(image, false);
				if (!set.ok)
					continue;
				// The cell's spots: 0 = centre, 2, 3, 4 = the others (offsets are a first approximation).
				float ox = 0, oy = 0;
				switch (o.sub_cell) {
				case 2: ox = -15; oy = 0; break;
				case 3: ox = 15; oy = 0; break;
				case 4: ox = 0; oy = 8; break;
				default: break;
				}
				const std::string seq = art.get_string("Sequence");
				int start = 0, step = 1;
				if (!seq.empty()) {
					const auto ready = ra::ini::IniFile::split_list(w.rules.art().get_string(seq, "Ready"));
					if (ready.size() >= 3) {
						ra::ini::parse_int(ready[0], start);
						ra::ini::parse_int(ready[2], step);
					}
				}
				const int dir8 = static_cast<int>(ra::Dir16(static_cast<uint16_t>(o.facing << 8)).facing<8>());
				add(set, start + dir8 * step, cx + ox, cy + oy, tint);
			}
		}
	}

	// Submit.
	atlas_.upload();
	stats_.atlas_images = atlas_.image_count();
	stats_.atlas_pages = atlas_.page_count();
	batch_.begin(view);
	PaletteSlot current = PaletteSlot::Tile;
	bool have_palette = false;
	int current_house = -1;
	for (const DrawCommand& cmd : commands) {
		if (!have_palette || cmd.palette != current || cmd.house != current_house) {
			current = cmd.palette;
			current_house = cmd.house;
			have_palette = true;
			if (current == PaletteSlot::Unit && current_house >= 0)
				batch_.set_palette(*house_palettes_[static_cast<size_t>(current_house)]);
			else
				batch_.set_palette(current == PaletteSlot::Tile ? tile_palette_ : (current == PaletteSlot::Object ? object_palette_ : unit_palette_));
		}
		batch_.draw(atlas_, cmd.rect, cmd.x, cmd.y, cmd.tint);
	}
	batch_.end();
	stats_.quads = batch_.quads_drawn();
	stats_.objects_drawn = commands.size() - stats_.tiles_drawn;
}

} // namespace ra::render
