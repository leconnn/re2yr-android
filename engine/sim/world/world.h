#pragma once

// A loaded map: its theater tiles, the cell grid, overlays and terrain objects, lighting,
// and the rules as the map modifies them.

#include "core/random.h"
#include "formats/map.h"
#include "rules/rules_db.h"
#include "sim/world/map_grid.h"
#include "sim/world/theater_tiles.h"

#include <memory>
#include <string>
#include <vector>

namespace ra::world {

// What the rules say about an overlay type (a type's index is the byte stored in the map).
struct OverlayInfo {
	std::string name;
	std::string image;             // art section / file base name
	LandType land = LandType::Clear;
	bool tiberium = false;         // ore and gems
	bool wall = false;
	bool crate = false;
	bool veins = false;
	bool no_use_tile_land_type = false;
	bool crushable = false;        // Crushable= (crushable walls: passability class 1)
	bool rubble = false;           // IsRubble=
	bool rock = false;             // IsARock=
	bool draw_flat = true;
};

// A tree, rock or other object from the map's [Terrain] section.
struct TerrainObject {
	int x = 0, y = 0;
	int type = -1;                 // index into the Terrain types of the rules
};

// A unit, infantry, building or aircraft the map starts with ([Units], [Infantry], [Structures], [Aircraft]).
struct PlacedObject {
	ra::rules::TypeKind kind = ra::rules::TypeKind::Vehicle;
	std::string owner;             // house name, as the map writes it
	int type = -1;                 // index into the types of 'kind'
	int health = 256;              // 0..256
	int x = 0, y = 0;              // cell
	int facing = 0;                // 0..255, 0 = north, clockwise
	int sub_cell = 0;              // infantry only: 0 = centre, 2, 3, 4 = the other spots
	std::string mission;
	int veterancy = 0;
};

// The map's [Lighting] section. Values scale the palette: red/green/blue tint the light and
// ambient sets its overall level. (Ion storm and dominator values are not read here.)
struct Lighting {
	double ambient = 1.0;
	double red = 1.0, green = 1.0, blue = 1.0;
	double ground = 0.0;
	double level = 0.0;

	// The brightness of a cell at a height level, in thousandths (1000 = normal, at most 2000).
	// Higher ground is brighter: ambient + Level * height - Ground, as CellClass::UpdateCellLighting
	// (0x484680) computes it with no light sources and no special effect running.
	int cell_intensity(int cell_level) const;
	// The colour cast of the light in thousandths per channel (the map's Red, Green and Blue).
	int red_1000() const;
	int green_1000() const;
	int blue_1000() const;
};

struct World {
	ra::formats::MapData map;
	ra::rules::GameKind game = ra::rules::GameKind::YurisRevenge;
	const ra::rules::TheaterDef* theater = nullptr;
	ra::rules::RulesDb rules; // the base rules with this map's overrides applied
	std::unique_ptr<TheaterTiles> tiles;
	MapGrid grid;
	std::vector<OverlayInfo> overlays;
	std::vector<TerrainObject> terrain_objects;
	Lighting lighting;
	std::vector<PlacedObject> placed_objects; // in the order the map lists them: structures, units, aircraft, infantry

	// Loads a map. 'base_mounts' is the number of archives mounted before any theater's
	// (the vfs's mount count after mount_standard); everything mounted after that is
	// removed and this map's theater archives are mounted in its place.
	// 'vfs' must outlive the World, which reads tile files from it on demand.
	// Returns false (and sets *error) if the map's theater isn't available in this game.
	// 'scenario_rng' is the game's synchronised generator. A map that fills its empty cells with
	// water draws one value per cell from it while loading (as the original does); pass null in
	// tools that don't simulate, which then pick the water variants without it.
	static bool load(ra::vfs::Vfs& vfs, size_t base_mounts, const ra::rules::RulesDb& base_rules,
	                 ra::formats::MapData map, World& out, std::string* error = nullptr, ra::Random2* scenario_rng = nullptr);

	// The theater tile a cell shows, or null if it has none.
	const ra::formats::TmpTile* tile_of(const MapCell& cell) const { return tiles ? tiles->tile(cell.tile, cell.sub_tile) : nullptr; }
};

} // namespace ra::world
