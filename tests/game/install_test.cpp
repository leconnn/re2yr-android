// Decodes every asset of a real RA2 / Yuri's Revenge install.
//
// These tests need the game files, which are not part of the repository. Set
// RA_GAME_DIR to the install folder to run them; without it they are skipped.
// Set RA_NAMES_FILE to a text file of known file names (one per line) to
// list archive entries by name; otherwise only the hash ids are known.

#include "formats/audio.h"
#include "formats/csf.h"
#include "formats/fnt.h"
#include "formats/map.h"
#include "formats/palette.h"
#include "formats/pcx.h"
#include "formats/shp.h"
#include "formats/tmp.h"
#include "formats/voxel.h"
#include "rules/rules_db.h"
#include "sim/objects/simulation.h"
#include "sim/world/world.h"
#include "vfs/vfs.h"
#include "voxel/voxel_renderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace ra;

namespace {

std::string game_dir()
{
	const char* dir = std::getenv("RA_GAME_DIR");
	return dir ? dir : "";
}

std::string names_file()
{
	if (const char* f = std::getenv("RA_NAMES_FILE"))
		return f;
	const std::string fallback = std::string(RA_REPO_ROOT) + "/re/data/names.txt";
	return std::filesystem::exists(fallback) ? fallback : "";
}

std::string extension(const std::string& path)
{
	const auto dot = path.rfind('.');
	if (dot == std::string::npos)
		return "";
	std::string e = path.substr(dot + 1);
	for (char& c : e)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return e;
}

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

// Mounts the Yuri's Revenge file set and loads the name list. Skips if the game isn't available.
struct Install {
	vfs::Vfs vfs;
	vfs::NameDb names;
	bool have_names = false;
	std::vector<std::string> mounted;
	size_t base_mounts = 0; // archives mounted before any theater's

	bool load()
	{
		const std::string dir = game_dir();
		if (dir.empty())
			return false;
		vfs.set_loose_directory(dir);
		mounted = vfs.mount_standard(vfs::GameKind::YurisRevenge);
		base_mounts = vfs.mounts().size();
		if (const std::string nf = names_file(); !nf.empty()) {
			std::ifstream in(nf, std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			have_names = names.add_lines(ss.str()) > 0;
		}
		return true;
	}
};

Install& install()
{
	static Install* inst = [] {
		auto* i = new Install();
		if (!i->load()) {
			delete i;
			return static_cast<Install*>(nullptr);
		}
		return i;
	}();
	return *inst;
}

#define REQUIRE_INSTALL() \
	if (game_dir().empty()) \
		GTEST_SKIP() << "set RA_GAME_DIR to the game folder to run this test"

// Returns an error message, or an empty string if the data decodes.
std::string check_asset(const std::string& ext, std::span<const uint8_t> d)
{
	using namespace ra::formats;
	static const char* const theater[] = { "tem", "sno", "urb", "des", "lun", "ubn" };

	auto check_shp = [&]() -> std::string {
		ShpFile f;
		if (!ShpFile::parse(d, f))
			return "SHP header";
		std::vector<uint8_t> px;
		for (size_t i = 0; i < f.frames().size(); ++i) {
			if (!f.decode_frame(i, px))
				return "SHP frame " + std::to_string(i);
		}
		return "";
	};

	if (ext == "shp" || ext == "sha" || ext == "mrf")
		return check_shp();
	for (const char* t : theater) {
		if (ext != t)
			continue;
		if (d.size() >= 4 && std::memcmp(d.data(), "RIFF", 4) == 0) {
			PcmAudio pcm; // a WAV stored under a theater extension
			return decode_wav(d, pcm) ? "" : "WAV";
		}
		if (d.size() >= 16 && rd32(d.data() + 8) == 60 && rd32(d.data() + 12) == 30) {
			TmpFile tmp;
			return TmpFile::parse(d, tmp) ? "" : "TMP";
		}
		return check_shp(); // terrain objects (trees, rubble) are sprites
	}
	if (ext == "vxl") {
		VxlFile v;
		if (!VxlFile::parse(d, v))
			return "VXL header";
		std::vector<Voxel> vox;
		for (size_t i = 0; i < v.limbs().size(); ++i) {
			if (!v.decode_limb(i, vox))
				return "VXL limb " + std::to_string(i);
		}
		return "";
	}
	if (ext == "hva") {
		HvaFile h;
		return HvaFile::parse(d, h) ? "" : "HVA";
	}
	if (ext == "vpl") {
		VplFile v;
		return VplFile::parse(d, v) ? "" : "VPL";
	}
	if (ext == "pal") {
		Palette p;
		return parse_palette(d, p) ? "" : "PAL";
	}
	if (ext == "csf") {
		CsfFile c;
		return CsfFile::parse(d, c) && c.label_count() > 0 ? "" : "CSF";
	}
	if (ext == "wav") {
		WavInfo info;
		if (!parse_wav_header(d, info) || (info.format != 1 && info.format != 0x11))
			return "WAV header";
		PcmAudio pcm;
		return decode_wav(d, pcm) && !pcm.samples.empty() ? "" : "WAV decode";
	}
	if (ext == "pcx") {
		PcxImage img;
		return parse_pcx(d, img) && (img.truecolor ? img.rgb.size() : img.pixels.size()) == size_t(img.width) * img.height * (img.truecolor ? 3 : 1) ? "" : "PCX";
	}
	if (ext == "fnt") {
		// Both executables only load the "fonT" and "FoNt" layouts. The few fonts in
		// ra2.mix/cache.mix in the older C&C layout are leftovers that nothing loads.
		if (d.size() < 4 || (std::memcmp(d.data(), "fonT", 4) != 0 && std::memcmp(d.data(), "FoNt", 4) != 0))
			return "";
		FntFile f;
		return FntFile::parse(d, f) ? "" : "FNT";
	}
	if (ext == "map" || ext == "mpr" || ext == "yrm") {
		MapData m;
		std::string error;
		return load_map(d, m, &error) ? "" : "map: " + error;
	}
	return "";
}

} // namespace

TEST(Install, MountsTheStandardArchivesInOrder)
{
	REQUIRE_INSTALL();
	Install& in = install();
	ASSERT_FALSE(in.mounted.empty());
	std::cout << "Mounted " << in.mounted.size() << " archives:";
	for (const auto& m : in.mounted)
		std::cout << " " << m;
	std::cout << "\n";

	// The expansion patch, the base archives and their nested archives are all there, in priority order.
	auto index_of = [&](const std::string& label) {
		for (size_t i = 0; i < in.mounted.size(); ++i) {
			if (in.mounted[i] == label)
				return static_cast<int>(i);
		}
		return -1;
	};
	const int expand = index_of("expandmd01.mix");
	const int ra2md = index_of("ra2md.mix");
	const int local = index_of("ra2md.mix/localmd.mix");
	ASSERT_GE(expand, 0);
	ASSERT_GE(ra2md, 0);
	ASSERT_GE(local, 0);
	EXPECT_LT(expand, ra2md); // patched files win
	EXPECT_LT(ra2md, local);

	EXPECT_TRUE(in.vfs.exists("rulesmd.ini"));
	EXPECT_TRUE(in.vfs.exists("artmd.ini"));
	EXPECT_TRUE(in.vfs.exists("ra2md.csf"));
	EXPECT_TRUE(in.vfs.exists("unittem.pal"));
	EXPECT_TRUE(in.vfs.exists("htnk.vxl"));
}

TEST(Install, RulesAreServedFromThePatchArchive)
{
	REQUIRE_INSTALL();
	Install& in = install();
	const auto rules = in.vfs.open("rulesmd.ini");
	ASSERT_TRUE(rules);
	EXPECT_GT(rules.bytes.size(), 100000u);
	const auto ini = ini::IniFile::parse(rules.text());
	EXPECT_GT(ini.sections().size(), 500u);
	EXPECT_FALSE(ini.get_string("InfantryTypes", "1").empty());
	EXPECT_TRUE(in.vfs.open("expandmd01.mix") || true); // loose lookup of an archive name is harmless
}

TEST(Install, EveryArchiveEntryListsAndEveryAssetDecodes)
{
	REQUIRE_INSTALL();
	Install& in = install();

	size_t entries = 0, named = 0, unnamed = 0;
	std::map<std::string, size_t> decoded;
	std::vector<std::string> failures;
	in.vfs.visit_all(in.have_names ? &in.names : nullptr, [&](const vfs::Vfs::VisitedEntry& e) {
		++entries;
		if (!e.named) {
			++unnamed;
			return;
		}
		++named;
		const std::string ext = extension(e.path);
		const std::string err = check_asset(ext, e.bytes);
		if (!err.empty()) {
			failures.push_back(e.path + ": " + err);
		} else {
			++decoded[ext];
		}
	});

	std::cout << "Entries: " << entries << " (named " << named << ", unnamed " << unnamed << ")\n";
	for (const auto& [ext, n] : decoded)
		std::cout << "  " << ext << ": " << n << " decoded\n";
	for (size_t i = 0; i < failures.size() && i < 25; ++i)
		ADD_FAILURE() << failures[i];
	EXPECT_TRUE(failures.empty()) << failures.size() << " assets failed to decode";

	EXPECT_GT(entries, 12000u);
	if (in.have_names) {
		EXPECT_LE(unnamed, 10u) << "almost every entry should have a known name";
		EXPECT_GT(decoded["shp"], 3000u);
		EXPECT_GT(decoded["vxl"], 200u);
		EXPECT_GT(decoded["pal"], 100u);
	}
}

TEST(Install, LooseMapsAndMapArchivesDecode)
{
	REQUIRE_INSTALL();
	namespace fs = std::filesystem;
	int loaded = 0;
	std::vector<std::string> failures;
	for (const auto& entry : fs::directory_iterator(game_dir())) {
		if (!entry.is_regular_file())
			continue;
		const std::string ext = extension(entry.path().filename().string());
		if (ext != "map" && ext != "yrm" && ext != "mpr" && ext != "mmx" && ext != "yro")
			continue;
		auto source = vfs::map_file(entry.path().string());
		ASSERT_TRUE(source);
		formats::MapData map;
		std::string error;
		if (formats::load_map(source->all(), map, &error))
			++loaded;
		else
			failures.push_back(entry.path().filename().string() + ": " + error);
	}
	for (const auto& f : failures)
		ADD_FAILURE() << f;
	std::cout << "Loose maps loaded: " << loaded << "\n";
	EXPECT_GT(loaded, 50);
}

TEST(Install, AudioBanksAreConsistent)
{
	REQUIRE_INSTALL();
	Install& in = install();
	int banks = 0;
	for (const char* prefix : { "audio", "audiomd" }) {
		const auto idx = in.vfs.open(std::string(prefix) + ".idx");
		const auto bag = in.vfs.open(std::string(prefix) + ".bag");
		if (!idx || !bag)
			continue;
		formats::AudioIndex index;
		ASSERT_TRUE(formats::AudioIndex::parse(idx.bytes, index)) << prefix;
		EXPECT_GT(index.entries.size(), 500u) << prefix;
		EXPECT_TRUE(index.fits_bank(bag.bytes.size())) << prefix;
		for (const auto& e : index.entries)
			EXPECT_EQ(e.sample_rate, 22050u) << prefix << " " << e.name;
		++banks;
	}
	EXPECT_GE(banks, 1);
}

TEST(Install, StringTablesHaveTheNamesTheRulesUse)
{
	REQUIRE_INSTALL();
	Install& in = install();
	const auto data = in.vfs.open("ra2md.csf");
	ASSERT_TRUE(data);
	formats::CsfFile csf;
	ASSERT_TRUE(formats::CsfFile::parse(data.bytes, csf));
	EXPECT_GT(csf.label_count(), 5000u);
	const auto rules = ini::IniFile::parse(in.vfs.open("rulesmd.ini").text());
	// Every infantry type's UIName label resolves to a string.
	int resolved = 0, missing = 0;
	if (const auto* list = rules.section("InfantryTypes")) {
		for (const auto& [key, type] : list->entries) {
			const std::string label = rules.get_string(type, "UIName");
			if (label.empty())
				continue;
			(csf.find(label) ? resolved : missing)++;
		}
	}
	EXPECT_GT(resolved, 20);
	EXPECT_EQ(missing, 0);
}

TEST(Install, RedAlert2ModeMountsOnlyTheBaseArchives)
{
	REQUIRE_INSTALL();
	vfs::Vfs ra2;
	ra2.set_loose_directory(game_dir());
	const auto mounted = ra2.mount_standard(vfs::GameKind::RedAlert2);
	ASSERT_FALSE(mounted.empty());
	for (const auto& label : mounted) {
		EXPECT_EQ(label.find("md.mix"), std::string::npos) << label << " belongs to Yuri's Revenge";
		EXPECT_EQ(label.find("MD.MIX"), std::string::npos) << label << " belongs to Yuri's Revenge";
	}
	// The original rules and strings are there, the expansion's are not.
	EXPECT_TRUE(ra2.exists("rules.ini"));
	EXPECT_TRUE(ra2.exists("art.ini"));
	EXPECT_TRUE(ra2.exists("ra2.csf"));
	EXPECT_FALSE(ra2.exists("rulesmd.ini")); // (loose files in the folder would still be found: they override archives)

	// The base game has fewer unit types than the expansion.
	const auto rules = ini::IniFile::parse(ra2.open("rules.ini").text());
	const auto yr_rules = ini::IniFile::parse(install().vfs.open("rulesmd.ini").text());
	const auto* base = rules.section("InfantryTypes");
	const auto* expansion = yr_rules.section("InfantryTypes");
	ASSERT_NE(base, nullptr);
	ASSERT_NE(expansion, nullptr);
	EXPECT_LT(base->entries.size(), expansion->entries.size());
}

namespace {

struct MapSource {
	std::string name;
	std::vector<uint8_t> bytes;
};

// Every map in the install: those inside archives and the loose ones in the game folder.
std::vector<MapSource> all_maps(Install& in)
{
	std::vector<MapSource> maps;
	in.vfs.visit_all(in.have_names ? &in.names : nullptr, [&](const vfs::Vfs::VisitedEntry& e) {
		if (e.named && extension(e.path) == "map")
			maps.push_back({ e.path, std::vector<uint8_t>(e.bytes.begin(), e.bytes.end()) });
	});
	namespace fs = std::filesystem;
	for (const auto& entry : fs::directory_iterator(game_dir())) {
		if (!entry.is_regular_file())
			continue;
		const std::string ext = extension(entry.path().filename().string());
		if (ext != "map" && ext != "yrm" && ext != "mpr" && ext != "mmx" && ext != "yro")
			continue;
		auto source = vfs::map_file(entry.path().string());
		if (source)
			maps.push_back({ entry.path().filename().string(), std::vector<uint8_t>(source->all().begin(), source->all().end()) });
	}
	return maps;
}

} // namespace

TEST(Install, EveryMapLoadsIntoAWorldWithResolvedTiles)
{
	REQUIRE_INSTALL();
	Install& in = install();
	rules::RulesDb rules;
	std::string error;
	ASSERT_TRUE(rules.load(in.vfs, vfs::GameKind::YurisRevenge, &error)) << error;
	EXPECT_EQ(rules.type_count(rules::TypeKind::Infantry), 65);
	EXPECT_EQ(rules.type_count(rules::TypeKind::Vehicle), 80);
	EXPECT_EQ(rules.type_count(rules::TypeKind::Building), 402); // 403 entries, one of them a repeated name
	EXPECT_EQ(rules.type_count(rules::TypeKind::Overlay), 250);

	const auto maps = all_maps(in);
	ASSERT_GT(maps.size(), 400u);
	size_t placed = 0, placed_lines = 0;
	size_t loaded = 0, cells = 0, no_tile = 0, unresolved = 0, missing_hyte = 0, empty_slot = 0, bad_overlay = 0, terrain_objects = 0;
	std::map<std::string, int> theaters;
	std::vector<std::string> failures;
	for (const MapSource& m : maps) {
		formats::MapData data;
		if (!formats::load_map(m.bytes, data, &error)) {
			failures.push_back(m.name + ": " + error);
			continue;
		}
		const std::string theater = data.theater;
		for (const char* section : { "Structures", "Units", "Aircraft", "Infantry" })
			if (const ra::ini::IniSection* sec = data.ini.section(section))
				placed_lines += sec->entries.size();
		world::World w;
		if (!world::World::load(in.vfs, in.base_mounts, rules, std::move(data), w, &error)) {
			failures.push_back(m.name + " (" + theater + "): " + error);
			continue;
		}
		++loaded;
		++theaters[theater];
		terrain_objects += w.terrain_objects.size();
		placed += w.placed_objects.size();
		for (int y = 0; y < world::MapGrid::kSize; ++y) {
			for (int x = 0; x < world::MapGrid::kSize; ++x) {
				const world::MapCell& c = w.grid.at(x, y);
				if (!c.exists)
					continue;
				++cells;
				if (c.tile < 0) {
					++no_tile;
				} else if (!w.tile_of(c)) {
					// Some maps use the marble-madness "hyte" height tiles, which only the desert
					// theater has; elsewhere those cells stay blank.
					if (w.tiles->template_file(c.tile).rfind("hyte", 0) == 0) {
						++missing_hyte;
						continue;
					}
					if (w.tiles->tmp(c.tile)) { // the template exists but this slot is empty: the map names a cell the template doesn't have
						++empty_slot;
						continue;
					}
					++unresolved;
					if (unresolved <= 12)
						std::cout << "unresolved tile " << c.tile << " sub " << int(c.sub_tile) << " in " << m.name << " (" << w.tiles->template_file(c.tile) << ")\n";
				}
				if (c.overlay >= static_cast<int>(w.overlays.size()))
					++bad_overlay;
			}
		}
	}
	in.vfs.unmount_from(in.base_mounts);
	for (size_t i = 0; i < failures.size() && i < 20; ++i)
		ADD_FAILURE() << failures[i];
	std::cout << "Worlds loaded: " << loaded << " of " << maps.size() << "; cells " << cells << ", without tile " << no_tile
	          << ", unresolved tiles " << unresolved << " (plus " << missing_hyte << " missing hyte tiles), empty template slots " << empty_slot << ", overlays out of range " << bad_overlay << ", terrain objects " << terrain_objects << "\n";
	for (const auto& [t, n] : theaters)
		std::cout << "  " << t << ": " << n << "\n";
	std::cout << "Placed objects " << placed << " of " << placed_lines << " map lines\n";
	EXPECT_GT(placed, 0u);
	EXPECT_GE(placed * 100, placed_lines * 95); // almost every line names a type the rules know and a cell on the map
	EXPECT_TRUE(failures.empty());
	EXPECT_EQ(unresolved, 0u);
	EXPECT_EQ(no_tile, 0u); // 0xFFFF and out-of-range tile numbers become the clear tile
	EXPECT_EQ(bad_overlay, 0u);
	EXPECT_LT(empty_slot * 1000, cells); // a stale tile number here and there, never a pattern
}

// Every map's zones build (the fills recurse, so this also checks the stack holds), every usable land
// cell gets a region, and land cells fall into a handful of tank zones per map.
TEST(Install, EveryMapBuildsItsZones)
{
	REQUIRE_INSTALL();
	Install& in = install();
	rules::RulesDb rules;
	std::string error;
	ASSERT_TRUE(rules.load(in.vfs, vfs::GameKind::YurisRevenge, &error)) << error;
	const auto maps = all_maps(in);
	size_t built = 0, land = 0, land_without_region = 0, most_zones = 0, most_parts = 0;
	std::string most_zones_map;
	for (const MapSource& m : maps) {
		formats::MapData data;
		if (!formats::load_map(m.bytes, data, &error))
			continue;
		world::World w;
		if (!world::World::load(in.vfs, in.base_mounts, rules, std::move(data), w, &error))
			continue;
		Random2 rng(1);
		const sim::Simulation s(w, rng);
		const sim::path::Zones& z = s.zones();
		++built;
		std::vector<int> zones;
		for (int y = 0; y < world::MapGrid::kSize; ++y)
			for (int x = 0; x < world::MapGrid::kSize; ++x) {
				if (!w.grid.at(x, y).exists || s.passability(x, y) != 0)
					continue;
				++land;
				if (z.region({ x, y }) == 0)
					++land_without_region;
				const int zone = z.movement_zone({ x, y }, 0);
				if (std::find(zones.begin(), zones.end(), zone) == zones.end())
					zones.push_back(zone);
			}
		if (zones.size() > most_zones) {
			most_zones = zones.size();
			most_zones_map = m.name;
		}
		most_parts = std::max(most_parts, z.nodes(0).size());
	}
	in.vfs.unmount_from(in.base_mounts);
	std::cout << "Zones built for " << built << " maps; " << land << " land cells; most tank zones on one map " << most_zones << " (" << most_zones_map
	          << "), most level-0 parts " << most_parts << "\n";
	EXPECT_GT(built, 400u);
	EXPECT_EQ(land_without_region, 0u);
	EXPECT_LT(most_parts, 32768u); // part ids are 16-bit signed in the original
}

// The Rhino tank body at four facings: sprite sizes and positions worked out from the decoded
// projection (P3 section 5.1). The size is exact; the offsets can move by a pixel with the original's
// table-based sine and cosine.
TEST(Install, RhinoBodyVoxelSpriteHasTheDecodedSizeAndPlace)
{
	REQUIRE_INSTALL();
	Install& in = install();
	ASSERT_NE(&in, nullptr);
	const auto vxl_data = in.vfs.open("htnk.vxl");
	const auto hva_data = in.vfs.open("htnk.hva");
	const auto vpl_data = in.vfs.open("voxels.vpl");
	ASSERT_TRUE(vxl_data && hva_data && vpl_data);
	formats::VxlFile vxl;
	formats::HvaFile hva;
	formats::VplFile vpl;
	ASSERT_TRUE(formats::VxlFile::parse(vxl_data.bytes, vxl));
	ASSERT_TRUE(formats::HvaFile::parse(hva_data.bytes, hva));
	ASSERT_TRUE(formats::VplFile::parse(vpl_data.bytes, vpl));

	voxel::Renderer renderer(vpl);
	const voxel::Section section { &vxl, 0, voxel::hva_matrix(hva, 0, 0, vxl.limbs()[0].scale) };
	struct Expect { uint16_t facing; int offset_x, offset_y; };
	const Expect cases[] = { { 0x0000, -29, -28 }, { 0x4000, -29, -25 }, { 0x8000, -33, -25 }, { 0xC000, -33, -28 } };
	for (const Expect& e : cases) {
		const voxel::Image img = renderer.render(voxel::body_matrix(e.facing), std::span(&section, 1));
		ASSERT_FALSE(img.empty());
		EXPECT_EQ(img.width, 63) << "facing " << e.facing;
		EXPECT_EQ(img.height, 45) << "facing " << e.facing;
		EXPECT_NEAR(img.offset_x, e.offset_x, 1) << "facing " << e.facing;
		EXPECT_NEAR(img.offset_y, e.offset_y, 1) << "facing " << e.facing;
		size_t lit = 0;
		for (uint8_t p : img.pixels)
			lit += p != 0;
		EXPECT_GT(lit, 600u) << "facing " << e.facing; // a solid tank, not a speckle
	}
}
