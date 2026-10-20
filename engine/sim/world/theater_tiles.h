#pragma once

// The terrain tiles of one theater (P1 section 6.3).
//
// A theater's control file (temperatmd.ini and friends) lists tile sets. A set is a
// base file name and a count: a set "Clear" with 1 file is clear01.tem; a set "cliff"
// with 3 files is cliff01.tem, cliff02.tem, cliff03.tem. A map refers to a tile by its
// running index over all files of all sets in order. Each file is a template of several
// cells, and the map's sub-tile number picks one of them.

#include "formats/palette.h"
#include "formats/tmp.h"
#include "ini/ini.h"
#include "rules/game_def.h"
#include "vfs/vfs.h"

#include <memory>
#include <string>
#include <vector>

namespace ra::world {

struct TileSet {
	std::string name;
	std::string file_name;   // base name of the files, without number and extension
	int files = 0;           // TilesInSet
	int first_index = 0;     // running index of this set's first file
	ra::formats::Rgb low_radar, high_radar;
	bool allow_to_place = true;
	// A "marble madness" set (a duplicate used by a special game mode) names the set whose files
	// a normal game uses instead, file for file. -1 for ordinary sets.
	int non_marble_madness = -1;
};

class TheaterTiles {
public:
	// Reads the theater's control file. The theater's archives must already be mounted.
	bool load(const ra::vfs::Vfs& vfs, ra::rules::GameKind kind, const ra::rules::TheaterDef& theater, std::string* error = nullptr);

	const ra::rules::TheaterDef* theater() const { return theater_; }
	const ra::ini::IniFile& ini() const { return ini_; }
	const std::vector<TileSet>& sets() const { return sets_; }
	int template_count() const { return template_count_; }

	// A value from the theater file's [General] section (ClearTile, WaterSet, BridgeSet...).
	int general(const char* key, int fallback = -1) const { return ini_.get_int("General", key, fallback); }

	// The set that a running template index belongs to, or -1.
	int set_of(int template_index) const;
	// The running index of a set's first file, or -1 for no such set.
	int first_index_of_set(int set) const;

	// Maps a tile number as saved in a map to the current numbering. A set that has gained or
	// lost files since the map was made is listed in the theater file with LastTilesInSet; the
	// numbers of everything after it shift (the original's 0x544E30). 0xFFFF and negative numbers
	// ("no tile") are returned unchanged.
	int fix_up_tile(int saved_tile) const;
	// The file name of a template ("clear01.tem"), or empty if the index is out of range.
	std::string template_file(int template_index) const;

	// The parsed template, or null if the index is out of range or the file is missing or broken.
	// Loaded on first use. Not thread-safe.
	const ra::formats::TmpFile* tmp(int template_index) const;
	// One tile of a template (null for an empty slot). The sub-tile wraps around the slot count,
	// as in the original.
	const ra::formats::TmpTile* tile(int template_index, int sub_tile) const;

private:
	struct Slot {
		bool tried = false;
		ra::vfs::FileData data;
		std::unique_ptr<ra::formats::TmpFile> tmp;
	};

	const ra::vfs::Vfs* vfs_ = nullptr;
	const ra::rules::TheaterDef* theater_ = nullptr;
	ra::ini::IniFile ini_;
	std::vector<TileSet> sets_;
	int template_count_ = 0;
	struct Shift {
		int old_end;  // saved numbers from here on move by 'delta'
		int delta;
	};
	std::vector<Shift> shifts_;
	mutable std::vector<Slot> slots_;
};

} // namespace ra::world
