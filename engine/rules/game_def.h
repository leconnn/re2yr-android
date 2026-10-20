#pragma once

// What differs between Red Alert 2 and Yuri's Revenge (P2 section 6).
//
// One engine plays both. A game is just a set of files: which rules, art, sound and
// theater INI files to read, and which archives to mount. Keys the base game doesn't
// know keep the engine's defaults, so RA2 content behaves as in RA2.

#include "vfs/vfs.h"

#include <span>
#include <string_view>
#include <vector>

namespace ra::rules {

using GameKind = ra::vfs::GameKind;

const char* game_name(GameKind kind);

// The INI and string files of a game.
struct GameFiles {
	const char* rules;
	const char* art;
	const char* sound;
	const char* eva;
	const char* theme;
	const char* ai;
	const char* multiplayer_modes;
	const char* strings; // CSF
};
const GameFiles& game_files(GameKind kind);

enum class Theater { Temperate, Snow, Urban, Desert, Lunar, NewUrban };

struct TheaterDef {
	Theater id;
	const char* map_name;      // [Map] Theater= value
	const char* extension;     // file extension of theater-specific files, without the dot
	const char* ini_yr;        // theater control file in Yuri's Revenge
	const char* ini_ra2;       // theater control file in Red Alert 2, or null if RA2 doesn't have this theater
	const char* tile_palette;  // palette of the terrain tiles
	const char* unit_palette;  // palette of units drawn in this theater
	const char* object_palette;// palette of terrain objects, overlays and buildings
	std::vector<const char*> yr_archives; // mounted in this order, missing ones skipped
	std::vector<const char*> ra2_archives;
};

std::span<const TheaterDef> theaters();
// Finds a theater by the name a map uses ("TEMPERATE", "NEWURBAN", ...), case-insensitively.
const TheaterDef* find_theater(std::string_view map_name);
const TheaterDef& theater_def(Theater id);
// Whether this game has the theater.
bool game_has_theater(GameKind kind, const TheaterDef& theater);
// The archives that provide a theater's tiles and objects in a game, in mount order.
std::span<const char* const> theater_archives(GameKind kind, const TheaterDef& theater);
// Mounts a theater's archives after the ones already mounted. The caller should unmount
// the previous theater first (Vfs::unmount_from). Returns how many archives were mounted.
size_t mount_theater(ra::vfs::Vfs& vfs, GameKind kind, const TheaterDef& theater);

} // namespace ra::rules
