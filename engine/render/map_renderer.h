#pragma once

// Draws a loaded map: terrain tiles (with their cliff overhangs), overlays (ore, walls,
// bridges...) and terrain objects (trees, rocks), lit as the map's [Lighting] section says.
//
// Everything is drawn as palette-indexed sprites through a SpriteBatch. Images are put in an
// atlas the first time they are needed. Coordinates are "world pixels": a cell (x, y) at
// height level h has the top corner of its 60 x 30 diamond at ((x - y) * 30, (x + y) * 15 - 15 * h).

#include "formats/palette.h"
#include "formats/shp.h"
#include "render/indexed_atlas.h"
#include "render/sprite_batch.h"
#include "sim/world/world.h"
#include "vfs/vfs.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ra::render {

class MapRenderer {
public:
	MapRenderer() = default;

	// Creates the GL objects. A GL context must be current and load_gl() must have succeeded.
	bool init();

	// Prepares to draw a world: loads its palettes and forgets images of the previous map.
	// The world and the vfs must outlive their use here.
	bool set_world(const ra::world::World* world, const ra::vfs::Vfs* vfs);

	// Draws the map into the current framebuffer (the caller clears it).
	void draw(const View& view);

	// The map's extent in world pixels: left, top, right, bottom of the playable area.
	struct Bounds {
		float left, top, right, bottom;
	};
	Bounds playable_bounds() const;

	struct Stats {
		size_t quads = 0;
		size_t tiles_drawn = 0;
		size_t objects_drawn = 0;
		size_t atlas_images = 0;
		int atlas_pages = 0;
	};
	const Stats& stats() const { return stats_; }

	// Toggles for debugging.
	bool draw_tiles = true;
	bool draw_overlays = true;
	bool draw_terrain_objects = true;
	bool draw_objects = true; // the units, infantry and buildings the map starts with
	bool use_lighting = true;

private:
	struct TileImages {
		AtlasRect diamond;
		AtlasRect extra;
		int extra_dx = 0, extra_dy = 0; // offset of the extra rectangle from the diamond's top-left
		bool built = false;
	};
	// The sprites of one object type (an overlay or a terrain object).
	struct SpriteSet {
		bool loaded = false;
		bool ok = false;
		ra::vfs::FileData data;
		ra::formats::ShpFile shp;
		bool use_theater_palette = true;
		std::vector<AtlasRect> frames;      // per frame, built on demand
		std::vector<uint8_t> frame_built;
	};

	// Sprites of buildings, infantry and vehicles (drawn with the unit palette and a house colour).
	SpriteSet& object_sprite_set(const std::string& image, bool new_theater);
	// The index of the unit palette with the house colour of 'owner' put in its remap range.
	int house_palette(const std::string& owner);

	const TileImages& tile_images(int tile, int sub_tile);
	SpriteSet& sprite_set(std::unordered_map<std::string, SpriteSet>& cache, const std::string& image);
	const AtlasRect* sprite_frame(SpriteSet& set, int frame);
	Tint tint_for_level(int level) const;

	const ra::world::World* world_ = nullptr;
	const ra::vfs::Vfs* vfs_ = nullptr;
	SpriteBatch batch_;
	IndexedAtlas atlas_;
	PaletteTexture tile_palette_, object_palette_, unit_palette_;
	std::unordered_map<uint64_t, TileImages> tile_cache_;
	std::unordered_map<std::string, SpriteSet> overlay_sprites_;
	std::unordered_map<std::string, SpriteSet> terrain_sprites_;
	std::unordered_map<std::string, SpriteSet> object_sprites_;
	ra::formats::Palette unit_colors_;
	std::vector<std::unique_ptr<PaletteTexture>> house_palettes_;
	std::unordered_map<std::string, int> house_palette_index_; // lower-case owner name
	Stats stats_;
};

} // namespace ra::render
