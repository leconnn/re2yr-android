#include "formats/map.h"

#include "core/base64.h"
#include "formats/compression.h"

#include <algorithm>
#include <cstdlib>

namespace ra::formats {

namespace {

void fail(std::string* error, const std::string& message)
{
	if (error)
		*error = message;
}

bool parse_four_ints(const std::string* value, int out[4])
{
	if (!value)
		return false;
	const auto items = ra::ini::IniFile::split_list(*value);
	if (items.size() < 4)
		return false;
	for (int i = 0; i < 4; ++i) {
		if (!ra::ini::parse_int(items[i], out[i]))
			return false;
	}
	return true;
}

} // namespace

bool unpack_section(const ra::ini::IniSection& section, std::vector<uint8_t>& out)
{
	// The lines are numbered 1, 2, 3...; the base64 text is their concatenation in file order.
	std::string text;
	for (const auto& [key, value] : section.entries)
		text += value;
	return base64_decode(text, out);
}

bool load_map_text(std::string_view text, MapData& out, std::string* error)
{
	out = MapData {};
	out.ini = ra::ini::IniFile::parse(text);
	const ra::ini::IniFile& ini = out.ini;

	out.theater = ini.get_string("Map", "Theater");
	int v[4];
	if (parse_four_ints(ini.get("Map", "Size"), v)) {
		out.map_x = v[0];
		out.map_y = v[1];
		out.map_width = v[2];
		out.map_height = v[3];
	}
	if (parse_four_ints(ini.get("Map", "LocalSize"), v)) {
		out.local_x = v[0];
		out.local_y = v[1];
		out.local_width = v[2];
		out.local_height = v[3];
	}

	std::vector<uint8_t> packed, bytes;
	if (const auto* s = ini.section("IsoMapPack5")) {
		if (!unpack_section(*s, packed) || !decode_chunks(packed, ChunkCodec::Lzo, bytes)) {
			fail(error, "IsoMapPack5 is corrupt");
			return false;
		}
		out.cells.reserve(bytes.size() / 11);
		for (size_t i = 0; i + 11 <= bytes.size(); i += 11) {
			const uint8_t* p = bytes.data() + i;
			IsoCell c;
			c.x = uint16_t(p[0] | (p[1] << 8));
			c.y = uint16_t(p[2] | (p[3] << 8));
			c.tile = static_cast<int32_t>(uint32_t(p[4]) | (uint32_t(p[5]) << 8) | (uint32_t(p[6]) << 16) | (uint32_t(p[7]) << 24));
			c.sub_tile = p[8];
			c.height = p[9];
			c.unused = p[10];
			out.cells.push_back(c);
		}
	}

	for (const char* name : { "OverlayPack", "OverlayDataPack" }) {
		const auto* s = ini.section(name);
		if (!s)
			continue;
		if (!unpack_section(*s, packed) || !decode_chunks(packed, ChunkCodec::Lcw, bytes) || bytes.size() != size_t(kOverlayGrid) * kOverlayGrid) {
			fail(error, std::string(name) + " is corrupt");
			return false;
		}
		(name[7] == 'D' ? out.overlay_data : out.overlay) = std::move(bytes);
		bytes.clear();
	}

	if (const auto* s = ini.section("PreviewPack")) {
		int pv[4];
		if (!parse_four_ints(ini.get("Preview", "Size"), pv)) {
			fail(error, "PreviewPack without [Preview] Size");
			return false;
		}
		if (!unpack_section(*s, packed) || !decode_chunks(packed, ChunkCodec::Lzo, bytes) || bytes.size() != size_t(pv[2]) * pv[3] * 3) {
			fail(error, "PreviewPack is corrupt");
			return false;
		}
		out.preview.width = static_cast<uint32_t>(pv[2]);
		out.preview.height = static_cast<uint32_t>(pv[3]);
		out.preview.rgb = std::move(bytes);
	}
	return true;
}

bool load_map_archive(const ra::vfs::MixArchive& archive, MapData& out, std::string* error)
{
	// The archive holds the scenario (.map) and a small .pkt file that lists it. The scenario is
	// the text entry that has a [Map] section.
	for (const auto& entry : archive.entries()) {
		const auto data = archive.data(entry);
		if (data.empty() || (data[0] != '[' && data[0] != ';'))
			continue;
		const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
		if (ra::ini::IniFile::parse(text).section("Map") == nullptr)
			continue;
		return load_map_text(text, out, error);
	}
	fail(error, "no scenario text in the archive");
	return false;
}

bool load_map(std::span<const uint8_t> bytes, MapData& out, std::string* error)
{
	if (ra::vfs::MixArchive::looks_like_archive(bytes)) {
		auto source = std::make_shared<ra::vfs::MemorySource>(std::vector<uint8_t>(bytes.begin(), bytes.end()));
		auto archive = ra::vfs::MixArchive::open(source, error);
		if (!archive)
			return false;
		return load_map_archive(*archive, out, error);
	}
	return load_map_text({ reinterpret_cast<const char*>(bytes.data()), bytes.size() }, out, error);
}

} // namespace ra::formats
