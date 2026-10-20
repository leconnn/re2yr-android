#pragma once

// Scenario maps (P1 section 6.7).
//
// A map is an INI file whose bulky data (terrain cells, overlays, the preview
// image) is packed into base64 sections. This reads the INI and unpacks
// those sections. Interpreting objects, triggers and so on happens in the
// simulation. Maps are stored as plain text (.map, .yrm, .mpr) or inside a
// small encrypted MIX together with a .pkt file (.mmx, .yro).

#include "ini/ini.h"
#include "vfs/mix_archive.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ra::formats {

// One terrain cell from [IsoMapPack5].
struct IsoCell {
	uint16_t x = 0, y = 0;
	int32_t tile = 0;     // tile index; -1 / 0xFFFF... means none
	uint8_t sub_tile = 0; // which cell of a multi-cell template
	uint8_t height = 0;   // height level
	uint8_t unused = 0;
};

struct MapPreviewImage {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> rgb; // width * height * 3
};

inline constexpr int kOverlayGrid = 512; // overlays are stored on a 512 x 512 grid, index = y * 512 + x
inline constexpr uint8_t kNoOverlay = 0xFF;

struct MapData {
	ra::ini::IniFile ini;
	std::string theater; // as written: TEMPERATE, SNOW, URBAN, DESERT, LUNAR, NEWURBAN
	int map_x = 0, map_y = 0, map_width = 0, map_height = 0;     // [Map] Size: the isometric diamond
	int local_x = 0, local_y = 0, local_width = 0, local_height = 0; // [Map] LocalSize: the playable area
	std::vector<IsoCell> cells;
	std::vector<uint8_t> overlay;      // 512 * 512 overlay type indices, or empty if the map has none
	std::vector<uint8_t> overlay_data; // 512 * 512 overlay frame / variant values, or empty
	MapPreviewImage preview;           // empty if the map has none
};

// Parses map text. Returns false (and sets *error) if a packed section is corrupt.
bool load_map_text(std::string_view text, MapData& out, std::string* error = nullptr);

// Loads a map from an archive (.mmx, .yro) by finding the entry that holds the scenario text.
bool load_map_archive(const ra::vfs::MixArchive& archive, MapData& out, std::string* error = nullptr);

// Loads a map from file contents, either text or an archive.
bool load_map(std::span<const uint8_t> bytes, MapData& out, std::string* error = nullptr);

// Decodes a packed section ("[Name]" with lines "1=...", "2=..." of base64) to bytes.
bool unpack_section(const ra::ini::IniSection& section, std::vector<uint8_t>& out);

} // namespace ra::formats
