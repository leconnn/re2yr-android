#include "rules/game_def.h"
#include "rules/key_table.h"
#include "rules/rule_object.h"
#include "rules/rules_db.h"

#include "core/fpu.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace ra::rules;

TEST(KeyTable, HasAboutTwoThousandKeysAndEveryDefaultParses)
{
	const auto& table = KeyTable::instance();
	EXPECT_GT(table.size(), 2100u);
	int checked = 0;
	for (const KeyInfo& k : table.all()) {
		if (!k.has_default || k.default_text[0] == '\0')
			continue;
		bool ok = true;
		int i;
		double d;
		bool b;
		ra::formats::Rgb rgb;
		RuleRect rect;
		switch (k.type) {
		case KeyType::Int: ok = read_int(k.default_text, i); break;
		case KeyType::Bool: ok = read_bool(k.default_text, b) || k.default_text[0] == '!' || std::strstr(k.default_text, "==") != nullptr; break;
		case KeyType::Double: ok = read_double(k.default_text, d); break;
		case KeyType::Leptons: ok = read_leptons(k.default_text, i) || std::string(k.default_text) == "-1" || std::string(k.default_text) == "-1.0"; break;
		case KeyType::Rgb: ok = read_rgb(k.default_text, rgb); break;
		case KeyType::Rect: ok = read_rect(k.default_text, rect); break;
		default: break;
		}
		EXPECT_TRUE(ok) << k.owner << "::" << k.key << " default \"" << k.default_text << "\"";
		++checked;
	}
	EXPECT_GT(checked, 800);
}

TEST(KeyTable, FindsKeysThroughBaseClasses)
{
	const auto& table = KeyTable::instance();
	// Cost is read by TechnoTypeClass, so every concrete techno type finds it.
	const KeyInfo* a = table.find("InfantryTypeClass", "Cost");
	const KeyInfo* b = table.find("BuildingTypeClass", "cost");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(a, b);
	EXPECT_EQ(table.find("WeaponTypeClass", "NoSuchKey"), nullptr);
	EXPECT_EQ(base_class("UnitTypeClass"), "TechnoTypeClass");
	EXPECT_EQ(base_class("WeaponTypeClass"), "");
}

TEST(RuleObject, DefaultsComeFromTheOriginal)
{
	// An empty section reads only built-in defaults (P2 section 2.2).
	const RuleObject weapon(nullptr, "WeaponTypeClass");
	EXPECT_EQ(weapon.get_int("Burst"), 1);
	// Two bool defaults are derived from other keys.
	const auto derived = ra::ini::IniFile::parse("[A]\nSpeedType=Float\n[B]\nNoRotate=yes\n[C]\n");
	EXPECT_TRUE(RuleObject(derived.section("A"), "BuildingTypeClass").get_bool("WaterBound"));
	EXPECT_FALSE(RuleObject(derived.section("C"), "BuildingTypeClass").get_bool("WaterBound"));
	EXPECT_FALSE(RuleObject(derived.section("B"), "BulletTypeClass").get_bool("Rotates"));
	EXPECT_TRUE(RuleObject(derived.section("C"), "BulletTypeClass").get_bool("Rotates"));
	EXPECT_EQ(weapon.get_int("LaserDuration"), 10);
	EXPECT_TRUE(weapon.get_bool("DecloakToFire"));
	EXPECT_TRUE(weapon.get_bool("FireWhileMoving"));
	EXPECT_FALSE(weapon.get_bool("IsLaser"));

	const RuleObject warhead(nullptr, "WarheadTypeClass");
	EXPECT_EQ(warhead.get_int("DelayKillFrames"), 5);
	EXPECT_TRUE(warhead.get_bool("AffectsAllies"));
	EXPECT_DOUBLE_EQ(warhead.get_double("PercentAtMax"), 1.0);
	EXPECT_DOUBLE_EQ(warhead.get_double("ProneDamage"), 1.0);

	const RuleObject bullet(nullptr, "BulletTypeClass");
	EXPECT_TRUE(bullet.get_bool("Shadow"));
	EXPECT_DOUBLE_EQ(bullet.get_double("Elasticity"), 0.75);
	EXPECT_EQ(bullet.get_int("SpawnDelay"), 3);

	const RuleObject unit(nullptr, "UnitTypeClass");
	EXPECT_EQ(unit.get_int("TechLevel"), 255);      // a type without TechLevel= is never buildable
	EXPECT_EQ(unit.get_int("BuildLimit"), 2147483647);
	EXPECT_EQ(unit.get_int("Ammo"), -1);
	EXPECT_DOUBLE_EQ(unit.get_double("BuildTimeMultiplier"), 1.0);
	EXPECT_EQ(unit.get_int("CloakingSpeed"), 7);
	// A key nothing knows reads as zero.
	EXPECT_EQ(unit.get_int("NoSuchKey"), 0);
	EXPECT_FALSE(unit.get_bool("NoSuchKey"));
	EXPECT_EQ(unit.get_string("NoSuchKey"), "");
}

TEST(RuleObject, ReadsValuesLikeTheOriginal)
{
	const auto ini = ra::ini::IniFile::parse(
		"[Gun]\n"
		"Damage=45\nHex=$1F\nZero=0x10\nROF=0.03\nPct=50%\nRange=5.5\nFlag=Yes\nOff=no\nColor=10, 300 ,5\n"
		"Verses=100%,90%,0%\nBox=1,2,30,40\nNotSet=-1\nSpaced = 7 ; comment\n");
	const RuleObject gun(ini.section("Gun"), "WeaponTypeClass");
	EXPECT_EQ(gun.get_int("Damage"), 45);
	EXPECT_EQ(gun.get_int("Hex"), 31);
	EXPECT_EQ(gun.get_int("Zero"), 0); // like sscanf("%d"): the "0x" prefix isn't special
	EXPECT_EQ(gun.get_int("Spaced"), 7);
	// Doubles are read as 32-bit floats.
	EXPECT_EQ(gun.get_double("ROF"), static_cast<double>(0.03f));
	EXPECT_NE(gun.get_double("ROF"), 0.03);
	// A percentage is the float value times 0.01, computed the game's way.
	EXPECT_EQ(gun.get_double("Pct"), ra::fpu::mul(50.0, 0.01));
	// Leptons: cells times 256, truncated.
	EXPECT_EQ(gun.get_leptons("Range"), 1408);
	EXPECT_EQ(gun.get_leptons("NotSet"), 0); // -1 means "not set": no default for this key, so zero
	EXPECT_TRUE(gun.get_bool("Flag"));
	EXPECT_FALSE(gun.get_bool("Off"));
	const auto c = gun.get_rgb("Color");
	EXPECT_EQ(c.r, 10);
	EXPECT_EQ(c.g, 255); // clamped
	EXPECT_EQ(c.b, 5);
	const auto verses = gun.get_list("Verses");
	ASSERT_EQ(verses.size(), 3u);
	EXPECT_EQ(verses[1], "90%");
	const auto box = gun.get_rect("Box");
	EXPECT_EQ(box.width, 30);
	EXPECT_EQ(box.height, 40);
	EXPECT_TRUE(gun.has("damage"));
	EXPECT_FALSE(gun.has("Missing"));
}

TEST(RulesDb, RegistersTypesByPositionAndAppliesMapOverrides)
{
	const std::string rules =
		"[General]\nSpeed=1\n"
		"[InfantryTypes]\n5=GI\n9=ENGINEER\n12=GI\n"      // keys are not positions; duplicates are ignored
		"[OverlayTypes]\n0=GASAND\n1=CYCL\n"
		"[Warheads]\n1=SA\n"
		"[GI]\nStrength=125\nCost=200\nImage=GIIMG\n"
		"[ENGINEER]\nStrength=75\n"
		"[SA]\nVerses=100%\n";
	const std::string art = "[GIIMG]\nTheater=yes\n";
	RulesDb db;
	RulesSources src;
	src.rules = rules;
	src.art = art;
	db.load_from_text(GameKind::YurisRevenge, src);

	ASSERT_EQ(db.type_count(TypeKind::Infantry), 2);
	EXPECT_EQ(db.type_name(TypeKind::Infantry, 0), "GI");
	EXPECT_EQ(db.type_index(TypeKind::Infantry, "engineer"), 1);
	EXPECT_EQ(db.type_index(TypeKind::Infantry, "NOPE"), -1);
	EXPECT_EQ(db.type_index(TypeKind::Overlay, "CYCL"), 1);
	EXPECT_EQ(db.object(TypeKind::Infantry, 0).get_int("Strength"), 125);
	EXPECT_EQ(db.object(TypeKind::Infantry, "GI").get_int("Cost"), 200);
	EXPECT_EQ(db.image_name(db.object(TypeKind::Infantry, "GI")), "GIIMG");
	EXPECT_EQ(db.image_name(db.object(TypeKind::Infantry, "ENGINEER")), "ENGINEER");
	EXPECT_TRUE(db.art_section("GIIMG").get_bool("Theater"));

	const auto map = ra::ini::IniFile::parse(
		"[Basic]\nName=Test\n"
		"[InfantryTypes]\n0=NEWGUY\n"
		"[GI]\nStrength=999\n"
		"[NEWGUY]\nStrength=10\n"
		"[Houses]\n0=Russians\n[Russians]\nCredits=5000\n"
		"[Units]\n0=Russians,HTNK,256,10,10,0,Guard,none,0,-1,0,0,0\n");
	db.apply_map(map);
	EXPECT_EQ(db.object(TypeKind::Infantry, "GI").get_int("Strength"), 999);  // overridden
	EXPECT_EQ(db.object(TypeKind::Infantry, "GI").get_int("Cost"), 200);      // kept
	ASSERT_EQ(db.type_count(TypeKind::Infantry), 3);
	EXPECT_EQ(db.type_index(TypeKind::Infantry, "NEWGUY"), 2);                // registered after the existing ones
	EXPECT_EQ(db.object(TypeKind::Infantry, "NEWGUY").get_int("Strength"), 10);
	EXPECT_FALSE(db.rules().section("Units"));                                // map data isn't rules
	EXPECT_FALSE(db.rules().section("Russians"));                             // neither is a house's setup
	EXPECT_EQ(db.rules().get_int("General", "Speed"), 1);
}

TEST(RulesDb, AppliesLooseOverridesAfterTheMainFile)
{
	RulesDb db;
	RulesSources src;
	src.rules = "[GI]\nStrength=125\nCost=200\n";
	src.rules_overrides = { "[GI]\nStrength=300\n", "[GI]\nStrength=400\n[X]\nA=1\n" };
	src.art = "";
	db.load_from_text(GameKind::RedAlert2, src);
	EXPECT_EQ(db.section("GI", "InfantryTypeClass").get_int("Strength"), 400);
	EXPECT_EQ(db.section("GI", "InfantryTypeClass").get_int("Cost"), 200);
	EXPECT_EQ(db.game(), GameKind::RedAlert2);
}

TEST(GameDef, TheatersAndFiles)
{
	EXPECT_STREQ(game_files(GameKind::YurisRevenge).rules, "rulesmd.ini");
	EXPECT_STREQ(game_files(GameKind::RedAlert2).rules, "rules.ini");
	EXPECT_STREQ(game_files(GameKind::RedAlert2).strings, "ra2.csf");
	ASSERT_EQ(theaters().size(), 6u);

	const TheaterDef* snow = find_theater("snow");
	ASSERT_NE(snow, nullptr);
	EXPECT_STREQ(snow->extension, "sno");
	EXPECT_NE(find_theater("NEWURBAN"), nullptr);
	EXPECT_EQ(find_theater("MARS"), nullptr);

	// Red Alert 2 only has three theaters.
	EXPECT_TRUE(game_has_theater(GameKind::RedAlert2, theater_def(Theater::Urban)));
	EXPECT_FALSE(game_has_theater(GameKind::RedAlert2, theater_def(Theater::Lunar)));
	EXPECT_TRUE(game_has_theater(GameKind::YurisRevenge, theater_def(Theater::Lunar)));
	EXPECT_TRUE(theater_archives(GameKind::RedAlert2, theater_def(Theater::Lunar)).empty());
	// Yuri's Revenge's expansion archive comes before the base one.
	const auto yr = theater_archives(GameKind::YurisRevenge, theater_def(Theater::Temperate));
	ASSERT_GE(yr.size(), 2u);
	EXPECT_STREQ(yr[0], "isotemmd.mix");
	EXPECT_STREQ(yr[1], "isotemp.mix");
}
