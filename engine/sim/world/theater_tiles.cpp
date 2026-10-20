#include "sim/world/theater_tiles.h"

#include <algorithm>
#include <cstdio>

namespace ra::world {

bool TheaterTiles::load(const ra::vfs::Vfs& vfs, ra::rules::GameKind kind, const ra::rules::TheaterDef& theater, std::string* error)
{
	vfs_ = &vfs;
	theater_ = &theater;
	sets_.clear();
	shifts_.clear();
	slots_.clear();
	template_count_ = 0;
	int old_running = 0; // the running index in the numbering the map was saved with

	const char* ini_name = kind == ra::rules::GameKind::YurisRevenge ? theater.ini_yr : theater.ini_ra2;
	if (!ini_name) {
		if (error)
			*error = std::string(ra::rules::game_name(kind)) + " has no " + theater.map_name + " theater";
		return false;
	}
	const auto data = vfs.open(ini_name);
	if (!data) {
		if (error)
			*error = std::string("cannot find ") + ini_name;
		return false;
	}
	ini_ = ra::ini::IniFile::parse(data.text());

	// The game reads [TileSet0000], [TileSet0001]... until one is missing.
	for (int i = 0;; ++i) {
		char name[32];
		std::snprintf(name, sizeof name, "TileSet%04d", i);
		const ra::ini::IniSection* s = ini_.section(name);
		if (!s)
			break;
		TileSet set;
		set.name = ini_.get_string(name, "SetName");
		set.file_name = ini_.get_string(name, "FileName");
		set.files = ini_.get_int(name, "TilesInSet", 0);
		set.first_index = template_count_;
		set.allow_to_place = ini_.get_bool(name, "AllowToPlace", true);
		set.non_marble_madness = ini_.get_int(name, "NonMarbleMadness", -1);
		const auto low = ra::ini::IniFile::split_list(ini_.get_string(name, "LowRadarColor"));
		const auto high = ra::ini::IniFile::split_list(ini_.get_string(name, "HighRadarColor"));
		auto rgb = [](const std::vector<std::string>& v) {
			ra::formats::Rgb c;
			int r = 0, g = 0, b = 0;
			if (v.size() >= 3 && ra::ini::parse_int(v[0], r) && ra::ini::parse_int(v[1], g) && ra::ini::parse_int(v[2], b))
				c = { uint8_t(r), uint8_t(g), uint8_t(b) };
			return c;
		};
		set.low_radar = rgb(low);
		set.high_radar = rgb(high);
		const int last = ini_.get_int(name, "LastTilesInSet", -1);
		if (last == -1 || last == set.files) {
			old_running += set.files;
		} else {
			old_running += last;
			shifts_.push_back({ old_running, set.files - last });
		}
		template_count_ += std::max(0, set.files);
		sets_.push_back(std::move(set));
	}
	if (sets_.empty()) {
		if (error)
			*error = std::string(ini_name) + " has no tile sets";
		return false;
	}
	slots_.resize(static_cast<size_t>(template_count_));
	return true;
}

int TheaterTiles::set_of(int template_index) const
{
	if (template_index < 0 || template_index >= template_count_)
		return -1;
	for (size_t i = 0; i < sets_.size(); ++i) {
		const TileSet& set = sets_[i];
		if (template_index >= set.first_index && template_index < set.first_index + set.files)
			return static_cast<int>(i);
	}
	return -1;
}

int TheaterTiles::first_index_of_set(int set) const
{
	return set >= 0 && set < static_cast<int>(sets_.size()) ? sets_[static_cast<size_t>(set)].first_index : -1;
}

int TheaterTiles::fix_up_tile(int saved_tile) const
{
	if (saved_tile < 0 || saved_tile == 0xFFFF)
		return saved_tile;
	int tile = saved_tile;
	for (const Shift& s : shifts_) {
		if (s.old_end > saved_tile)
			break;
		tile += s.delta;
	}
	return tile;
}

std::string TheaterTiles::template_file(int template_index) const
{
	const int set = set_of(template_index);
	if (set < 0)
		return {};
	const TileSet* s = &sets_[static_cast<size_t>(set)];
	const int number = template_index - s->first_index + 1;
	// In a normal game the marble madness sets use another set's files, file for file.
	if (s->non_marble_madness >= 0 && s->non_marble_madness < static_cast<int>(sets_.size())) {
		const TileSet& alias = sets_[static_cast<size_t>(s->non_marble_madness)];
		if (number <= alias.files)
			s = &alias;
	}
	char suffix[8];
	std::snprintf(suffix, sizeof suffix, "%02d", number);
	return s->file_name + suffix + "." + theater_->extension;
}

const ra::formats::TmpFile* TheaterTiles::tmp(int template_index) const
{
	if (template_index < 0 || template_index >= template_count_)
		return nullptr;
	Slot& slot = slots_[static_cast<size_t>(template_index)];
	if (!slot.tried) {
		slot.tried = true;
		const std::string file = template_file(template_index);
		if (!file.empty()) {
			slot.data = vfs_->open(file);
			if (slot.data) {
				auto parsed = std::make_unique<ra::formats::TmpFile>();
				if (ra::formats::TmpFile::parse(slot.data.bytes, *parsed))
					slot.tmp = std::move(parsed);
			}
		}
	}
	return slot.tmp.get();
}

const ra::formats::TmpTile* TheaterTiles::tile(int template_index, int sub_tile) const
{
	const ra::formats::TmpFile* t = tmp(template_index);
	if (!t || t->tiles().empty())
		return nullptr;
	const ra::formats::TmpTile& tile = t->tiles()[static_cast<size_t>(sub_tile) % t->tiles().size()];
	return tile.present ? &tile : nullptr;
}

} // namespace ra::world
