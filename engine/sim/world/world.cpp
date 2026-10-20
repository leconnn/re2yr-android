#include "sim/world/world.h"

#include "core/fpu.h"

#include <algorithm>

namespace ra::world {

namespace {

std::vector<OverlayInfo> read_overlays(const ra::rules::RulesDb& rules)
{
	using ra::rules::TypeKind;
	std::vector<OverlayInfo> infos;
	for (int i = 0; i < rules.type_count(TypeKind::Overlay); ++i) {
		const ra::rules::RuleObject o = rules.object(TypeKind::Overlay, i);
		OverlayInfo info;
		info.name = std::string(rules.type_name(TypeKind::Overlay, i));
		info.image = rules.image_name(o);
		info.tiberium = o.get_bool("Tiberium");
		info.wall = o.get_bool("Wall");
		info.crate = o.get_bool("Crate");
		info.veins = o.get_bool("IsVeins");
		info.no_use_tile_land_type = o.get_bool("NoUseTileLandType");
		info.draw_flat = o.get_bool("DrawFlat");
		// The land type of the overlay itself: an explicit Land= key, or Wall for walls.
		const std::string land = o.get_string("Land");
		info.land = land.empty() ? (info.wall ? LandType::Wall : LandType::Clear) : land_type_from_name(land.c_str());
		if (info.land == LandType::None)
			info.land = LandType::Clear;
		infos.push_back(std::move(info));
	}
	return infos;
}

// CellClass::RecalcAttributes (P3 section 8.7), for all cells.
void recalc_attributes(World& w)
{
	MapGrid& grid = w.grid;
	for (int y = 0; y < MapGrid::kSize; ++y) {
		for (int x = 0; x < MapGrid::kSize; ++x) {
			MapCell& c = *grid.mutable_at(x, y);
			if (!c.exists)
				continue;

			const ra::formats::TmpTile* tile = w.tile_of(c);
			c.slope = tile ? tile->ramp_type : 0;
			const LandType tile_land = tile ? land_type_from_tile(tile->land_type) : LandType::None;

			const OverlayInfo* ov = (c.overlay >= 0 && c.overlay < static_cast<int>(w.overlays.size())) ? &w.overlays[static_cast<size_t>(c.overlay)] : nullptr;
			if (ov && (ov->land == LandType::Wall || ov->land == LandType::Railroad || ov->no_use_tile_land_type)) {
				c.land = ov->land;
				if (ov->tiberium && c.slope > 0) { // ore can't lie on a slope
					c.overlay = -1;
					c.overlay_data = 0;
				}
			} else if (!tile) {
				c.land = (ov && !ov->no_use_tile_land_type) ? ov->land : LandType::Clear;
			} else if (!ov) {
				c.land = tile_land;
			} else if (ov->tiberium) {
				if (c.slope < 5) {
					c.land = ov->land == LandType::Clear ? LandType::Tiberium : tile_land;
				} else {
					c.overlay = -1;
					c.overlay_data = 0;
					c.land = tile_land;
				}
			} else {
				c.land = tile_land;
			}
		}
	}

	// Rules.CliffBackImpassability = 2: cells in front of a cliff face are rock.
	if (w.rules.section("General").get_int("CliffBackImpassability") == 2) {
		static const int offsets[6][2] = { { 0, -1 }, { -1, 0 }, { 2, 2 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
		for (int y = 0; y < MapGrid::kSize; ++y) {
			for (int x = 0; x < MapGrid::kSize; ++x) {
				MapCell& c = *grid.mutable_at(x, y);
				if (!c.exists)
					continue;
				if (c.land != LandType::Clear && c.land != LandType::Water && c.land != LandType::Beach && c.land != LandType::Ice)
					continue;
				for (const auto& o : offsets) {
					if (MapGrid::in_grid(x + o[0], y + o[1]) && grid.at(x + o[0], y + o[1]).level >= c.level + 4) {
						c.land = LandType::Rock;
						break;
					}
				}
			}
		}
	}
}

} // namespace

namespace {

int clamp_light(int v)
{
	return v < 0 ? 0 : (v > 2000 ? 2000 : v);
}

int to_1000(double v)
{
	return clamp_light(ra::fpu::ftol(ra::fpu::mul(v, 1000.0)));
}

// Reads one pre-placed object line: the fields are positional, and the layout differs by section.
void load_placed_section(const ra::ini::IniFile& ini, const char* section_name, ra::rules::TypeKind kind, World& w)
{
	const ra::ini::IniSection* section = ini.section(section_name);
	if (!section)
		return;
	for (const auto& [key, value] : section->entries) {
		const std::vector<std::string> f = ra::ini::IniFile::split_list(value);
		if (f.size() < 5)
			continue;
		auto num = [&](size_t i, int fallback) {
			int v;
			return i < f.size() && ra::ini::parse_int(f[i], v) ? v : fallback;
		};
		PlacedObject o;
		o.kind = kind;
		o.owner = f[0];
		o.type = w.rules.type_index(kind, f[1]);
		if (o.type < 0)
			continue;
		o.health = num(2, 256);
		o.x = num(3, 0);
		o.y = num(4, 0);
		if (!MapGrid::in_grid(o.x, o.y))
			continue;
		switch (kind) {
		case ra::rules::TypeKind::Infantry: // Owner,Type,Health,X,Y,SubCell,Mission,Facing,Tag,Veterancy
			o.sub_cell = num(5, 0);
			if (f.size() > 6) o.mission = f[6];
			o.facing = num(7, 0);
			o.veterancy = num(9, 0);
			break;
		case ra::rules::TypeKind::Building: // Owner,Type,Health,X,Y,Facing,Tag,...
			o.facing = num(5, 0);
			break;
		default: // Units, aircraft: Owner,Type,Health,X,Y,Facing,Mission,Tag,Veterancy
			o.facing = num(5, 0);
			if (f.size() > 6) o.mission = f[6];
			o.veterancy = num(8, 0);
			break;
		}
		w.placed_objects.push_back(std::move(o));
	}
}

void load_placed_objects(const ra::ini::IniFile& ini, World& w)
{
	using ra::rules::TypeKind;
	load_placed_section(ini, "Structures", TypeKind::Building, w);
	load_placed_section(ini, "Units", TypeKind::Vehicle, w);
	load_placed_section(ini, "Aircraft", TypeKind::Aircraft, w);
	load_placed_section(ini, "Infantry", TypeKind::Infantry, w);
}

} // namespace

int Lighting::cell_intensity(int cell_level) const
{
	// The scenario keeps Ambient as a percentage and Level and Ground in thousandths.
	const int ambient_percent = ra::fpu::ftol(ra::fpu::mul(ambient, 100.0));
	const int level_1000 = ra::fpu::ftol(ra::fpu::mul(this->level, 1000.0));
	const int ground_1000 = ra::fpu::ftol(ra::fpu::mul(ground, 1000.0));
	return clamp_light(ambient_percent * 1000 / 100 + level_1000 * cell_level - ground_1000);
}

int Lighting::red_1000() const { return to_1000(red); }
int Lighting::green_1000() const { return to_1000(green); }
int Lighting::blue_1000() const { return to_1000(blue); }

bool World::load(ra::vfs::Vfs& vfs, size_t base_mounts, const ra::rules::RulesDb& base_rules, ra::formats::MapData map, World& out, std::string* error, ra::Random2* scenario_rng)
{
	out = World();
	out.game = base_rules.game();

	const ra::rules::TheaterDef* theater = ra::rules::find_theater(map.theater);
	if (!theater) {
		if (error)
			*error = "unknown theater \"" + map.theater + "\"";
		return false;
	}
	if (!ra::rules::game_has_theater(out.game, *theater)) {
		if (error)
			*error = std::string(ra::rules::game_name(out.game)) + " has no " + theater->map_name + " theater";
		return false;
	}
	out.theater = theater;

	vfs.unmount_from(base_mounts);
	ra::rules::mount_theater(vfs, out.game, *theater);

	out.tiles = std::make_unique<TheaterTiles>();
	if (!out.tiles->load(vfs, out.game, *theater, error))
		return false;

	out.rules = base_rules;
	out.rules.apply_map(map.ini);
	out.overlays = read_overlays(out.rules);

	// [Lighting]
	{
		const ra::ini::IniFile& ini = map.ini;
		auto get = [&](const char* key, double fallback) {
			double v;
			const std::string* t = ini.get("Lighting", key);
			return t && ra::rules::read_double(*t, v) ? v : fallback;
		};
		out.lighting = { get("Ambient", 1.0), get("Red", 1.0), get("Green", 1.0), get("Blue", 1.0), get("Ground", 0.0), get("Level", 0.0) };
	}

	out.grid.set_size(map.map_width, map.map_height, map.local_x, map.local_y, map.local_width, map.local_height);

	// Every cell of the map's diamond starts as the fill tile ([Map] Fill=, clear by default) at
	// the map's base level; the terrain records then replace the cells they list. Cells outside
	// the diamond do not exist and their records are ignored.
	const int clear_first = out.tiles->first_index_of_set(out.tiles->general("ClearTile", 0));
	{
		const bool water = ra::ini::to_lower(map.ini.get_string("Map", "Fill", "Clear")) == "water";
		const int base_level = map.ini.get_int("Map", "Level", 0);
		const int water_first = out.tiles->first_index_of_set(out.tiles->general("WaterSet", -1));
		for (int y = 0; y < MapGrid::kSize; ++y) {
			for (int x = 0; x < MapGrid::kSize; ++x) {
				if (!out.grid.in_map(x, y))
					continue;
				MapCell& c = *out.grid.mutable_at(x, y);
				c.exists = true;
				c.sub_tile = 0;
				c.level = static_cast<int8_t>(base_level);
				if (water && water_first >= 0) {
					// Four water variants, one chosen per cell.
					const int variant = scenario_rng ? scenario_rng->ranged(0, 3) : ((x * 7 + y * 13) & 3);
					c.tile = water_first + variant;
				} else {
					c.tile = clear_first;
				}
			}
		}
	}
	for (const ra::formats::IsoCell& iso : map.cells) {
		MapCell* c = out.grid.mutable_at(iso.x, iso.y);
		if (!c || !c->exists)
			continue;
		const int tile = out.tiles->fix_up_tile(iso.tile);
		if (tile >= 0 && tile < out.tiles->template_count()) {
			c->tile = tile;
			c->sub_tile = iso.sub_tile;
		} else {
			// 0xFFFF (the editor's "default") or a number beyond the theater's tiles: the original
			// shows the theater's clear tile.
			c->tile = clear_first;
			c->sub_tile = 0;
		}
		c->level = static_cast<int8_t>(iso.height);
	}
	if (map.overlay.size() == size_t(MapGrid::kSize) * MapGrid::kSize) {
		for (int y = 0; y < MapGrid::kSize; ++y) {
			for (int x = 0; x < MapGrid::kSize; ++x) {
				const size_t i = size_t(y) * MapGrid::kSize + x;
				if (map.overlay[i] == ra::formats::kNoOverlay)
					continue;
				MapCell* c = out.grid.mutable_at(x, y);
				c->overlay = map.overlay[i];
				c->overlay_data = i < map.overlay_data.size() ? map.overlay_data[i] : 0;
			}
		}
	}

	// [Terrain]: "y * 1000 + x = TYPE"
	if (const ra::ini::IniSection* terrain = map.ini.section("Terrain")) {
		for (const auto& [key, value] : terrain->entries) {
			int code;
			if (!ra::ini::parse_int(key, code))
				continue;
			const int type = out.rules.type_index(ra::rules::TypeKind::Terrain, value);
			if (type < 0)
				continue;
			out.terrain_objects.push_back({ code % 1000, code / 1000, type });
		}
	}

	load_placed_objects(map.ini, out);

	recalc_attributes(out);
	out.map = std::move(map);
	return true;
}

} // namespace ra::world
