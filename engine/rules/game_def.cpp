#include "rules/game_def.h"

#include "ini/ini.h"

namespace ra::rules {

const char* game_name(GameKind kind)
{
	return kind == GameKind::YurisRevenge ? "Yuri's Revenge" : "Red Alert 2";
}

const GameFiles& game_files(GameKind kind)
{
	static const GameFiles yr = { "rulesmd.ini", "artmd.ini", "soundmd.ini", "evamd.ini", "thememd.ini", "aimd.ini", "mpmodesmd.ini", "ra2md.csf" };
	static const GameFiles ra2 = { "rules.ini", "art.ini", "sound.ini", "eva.ini", "theme.ini", "ai.ini", "mpmodes.ini", "ra2.csf" };
	return kind == GameKind::YurisRevenge ? yr : ra2;
}

namespace {

const std::vector<TheaterDef>& all_theaters()
{
	static const std::vector<TheaterDef> t = {
		{ Theater::Temperate, "TEMPERATE", "tem", "temperatmd.ini", "temperat.ini", "isotem.pal", "unittem.pal", "temperat.pal",
		  { "isotemmd.mix", "isotemp.mix", "temperat.mix", "tem.mix" }, { "isotemp.mix", "temperat.mix", "tem.mix" } },
		{ Theater::Snow, "SNOW", "sno", "snowmd.ini", "snow.ini", "isosno.pal", "unitsno.pal", "snow.pal",
		  { "isosnomd.mix", "isosnow.mix", "snowmd.mix", "snow.mix", "sno.mix" }, { "isosnow.mix", "snow.mix", "sno.mix" } },
		{ Theater::Urban, "URBAN", "urb", "urbanmd.ini", "urban.ini", "isourb.pal", "uniturb.pal", "urban.pal",
		  { "isourbmd.mix", "isourb.mix", "urban.mix", "urb.mix" }, { "isourb.mix", "urban.mix", "urb.mix" } },
		{ Theater::Desert, "DESERT", "des", "desertmd.ini", nullptr, "isodes.pal", "unitdes.pal", "desert.pal",
		  { "isodesmd.mix", "isodes.mix", "desert.mix", "des.mix" }, {} },
		{ Theater::Lunar, "LUNAR", "lun", "lunarmd.ini", nullptr, "isolun.pal", "unitlun.pal", "lunar.pal",
		  { "isolunmd.mix", "isolun.mix", "lunar.mix", "lun.mix" }, {} },
		{ Theater::NewUrban, "NEWURBAN", "ubn", "urbannmd.ini", nullptr, "isoubn.pal", "unitubn.pal", "urbann.pal",
		  { "isoubnmd.mix", "isoubn.mix", "urbann.mix", "ubn.mix" }, {} },
	};
	return t;
}

} // namespace

std::span<const TheaterDef> theaters()
{
	return all_theaters();
}

const TheaterDef* find_theater(std::string_view map_name)
{
	const std::string key = ra::ini::to_lower(ra::ini::trim(map_name));
	for (const TheaterDef& t : all_theaters()) {
		if (ra::ini::to_lower(t.map_name) == key)
			return &t;
	}
	return nullptr;
}

const TheaterDef& theater_def(Theater id)
{
	return all_theaters()[static_cast<size_t>(id)];
}

bool game_has_theater(GameKind kind, const TheaterDef& theater)
{
	return kind == GameKind::YurisRevenge || theater.ini_ra2 != nullptr;
}

std::span<const char* const> theater_archives(GameKind kind, const TheaterDef& theater)
{
	const auto& list = kind == GameKind::YurisRevenge ? theater.yr_archives : theater.ra2_archives;
	return { list.data(), list.size() };
}

size_t mount_theater(ra::vfs::Vfs& vfs, GameKind kind, const TheaterDef& theater)
{
	size_t mounted = 0;
	for (const char* name : theater_archives(kind, theater)) {
		if (vfs.mount(name))
			++mounted;
	}
	return mounted;
}

} // namespace ra::rules
