#include "formats/audio.h"
#include "formats/csf.h"
#include "formats/fnt.h"
#include "formats/map.h"
#include "formats/palette.h"
#include "formats/pcx.h"
#include "formats/shp.h"
#include "formats/tmp.h"
#include "formats/voxel.h"
#include "ini/ini.h"
#include "vfs/mix_archive.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>

using namespace ra::formats;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); }
void put32(Bytes& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
void putf(Bytes& v, float f) { uint32_t u; std::memcpy(&u, &f, 4); put32(v, u); }
void puts(Bytes& v, const char* s, size_t pad_to = 0)
{
	const size_t n = std::strlen(s);
	for (size_t i = 0; i < n; ++i)
		v.push_back(uint8_t(s[i]));
	for (size_t i = n; i < pad_to; ++i)
		v.push_back(0);
}

std::string read_file(const std::string& name)
{
	std::ifstream in(ra::test::data_dir() + "/" + name, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace

// ---- INI ----

TEST(Ini, ParsesSectionsKeysCommentsAndMerges)
{
	const auto ini = ra::ini::IniFile::parse(
		"; header comment\r\n"
		"[General]\r\n"
		"Speed = 5 ; trailing comment\r\n"
		"name=Hello World\r\n"
		"junk line without equals\r\n"
		"\r\n"
		"[general]\r\n"          // same section again, different case
		"Extra=1\r\n"
		"SPEED=7\r\n"            // later value replaces the earlier one
		"[Other]\r\n"
		"Flag=Yes\r\n"
		"Pct=50%\r\n"
		"Hex=$1F\r\n"
		"List=a, b ,c\r\n");
	EXPECT_EQ(ini.sections().size(), 2u);
	EXPECT_EQ(ini.get_int("General", "speed"), 7);
	EXPECT_EQ(ini.get_string("GENERAL", "Name"), "Hello World");
	EXPECT_EQ(ini.get_int("general", "extra"), 1);
	EXPECT_EQ(ini.get_int("General", "missing", 99), 99);
	EXPECT_TRUE(ini.get_bool("Other", "Flag"));
	EXPECT_DOUBLE_EQ(ini.get_percent("Other", "Pct"), 0.5);
	EXPECT_EQ(ini.get_int("Other", "Hex"), 31);
	const auto list = ra::ini::IniFile::split_list(ini.get_string("Other", "List"));
	ASSERT_EQ(list.size(), 3u);
	EXPECT_EQ(list[1], "b");
	// Entries keep their file order, and a replaced key keeps its position.
	EXPECT_EQ(ini.section("General")->entries[0].first, "Speed");
	EXPECT_EQ(ini.section("General")->entries[0].second, "7");

	auto over = ra::ini::IniFile::parse("[General]\nspeed=9\nNew=2\n[Added]\nx=1\n");
	auto merged = ini;
	merged.merge(over);
	EXPECT_EQ(merged.get_int("General", "Speed"), 9);
	EXPECT_EQ(merged.get_int("General", "New"), 2);
	EXPECT_EQ(merged.get_string("General", "name"), "Hello World");
	EXPECT_EQ(merged.get_int("Added", "x"), 1);
}

TEST(Ini, Booleans)
{
	bool b = false;
	for (const char* yes : { "yes", "Yes", "true", "TRUE", "1", "y", "t" }) {
		ASSERT_TRUE(ra::ini::parse_bool(yes, b)) << yes;
		EXPECT_TRUE(b) << yes;
	}
	for (const char* no : { "no", "No", "false", "0", "n", "f" }) {
		ASSERT_TRUE(ra::ini::parse_bool(no, b)) << no;
		EXPECT_FALSE(b) << no;
	}
	EXPECT_FALSE(ra::ini::parse_bool("maybe", b));
	EXPECT_FALSE(ra::ini::parse_bool("", b));
}

TEST(Ini, KeepsBase64LinesWithPadding)
{
	const auto ini = ra::ini::IniFile::parse("[Pack]\n1=QUJD\n2=REVG==\n");
	EXPECT_EQ(ini.get_string("Pack", "2"), "REVG==");
}

// ---- Palette ----

TEST(Palette, WidensSixBitComponents)
{
	Bytes raw(768, 0);
	raw[3] = 63;
	raw[4] = 32;
	raw[5] = 1;
	Palette pal;
	ASSERT_TRUE(parse_palette(raw, pal));
	EXPECT_EQ(pal.colors[1].r, 255);
	EXPECT_EQ(pal.colors[1].g, 130);
	EXPECT_EQ(pal.colors[1].b, 4);
	raw[0] = 64; // out of range for 6-bit
	EXPECT_FALSE(parse_palette(raw, pal));
	EXPECT_FALSE(parse_palette(Bytes(767), pal));
}

// ---- SHP ----

namespace {

Bytes build_shp(const std::vector<Bytes>& frame_data, const std::vector<uint32_t>& flags, uint16_t w, uint16_t h)
{
	Bytes v;
	put16(v, 0);
	put16(v, w);
	put16(v, h);
	put16(v, uint32_t(frame_data.size()));
	uint32_t offset = 8 + 24 * uint32_t(frame_data.size());
	for (size_t i = 0; i < frame_data.size(); ++i) {
		put16(v, 0); put16(v, 0); put16(v, w); put16(v, h);
		put32(v, flags[i]);
		put32(v, 0x00112233);
		put32(v, 0);
		put32(v, frame_data[i].empty() ? 0 : offset);
		offset += uint32_t(frame_data[i].size());
	}
	for (const auto& f : frame_data)
		v.insert(v.end(), f.begin(), f.end());
	return v;
}

} // namespace

TEST(Shp, DecodesRawPerLineAndRunLengthFrames)
{
	const Bytes raw = { 1, 2, 3, 4, 5, 6 };                                  // 3x2 raw
	const Bytes per_line = { 5, 0, 1, 2, 3, 5, 0, 4, 5, 6 };                // 3x2, two lines of (len=5, 3 pixels)
	const Bytes rle = { 6, 0, 7, 0, 1, 9, 5, 0, 0, 2, 8 };                  // line1: 7, 1 transparent, 9; line2: 2 transparent, 8
	const Bytes shp = build_shp({ raw, per_line, rle, {} }, { 0, 2, 3, 0 }, 3, 2);
	ShpFile f;
	ASSERT_TRUE(ShpFile::parse(shp, f));
	EXPECT_EQ(f.frames().size(), 4u);
	EXPECT_EQ(f.frames()[0].radar_color, 0x00112233u);

	std::vector<uint8_t> px;
	ASSERT_TRUE(f.decode_frame(0, px));
	EXPECT_EQ(px, (Bytes { 1, 2, 3, 4, 5, 6 }));
	ASSERT_TRUE(f.decode_frame(1, px));
	EXPECT_EQ(px, (Bytes { 1, 2, 3, 4, 5, 6 }));
	ASSERT_TRUE(f.decode_frame(2, px));
	EXPECT_EQ(px, (Bytes { 7, 0, 9, 0, 0, 8 }));
	ASSERT_TRUE(f.decode_frame(3, px)); // empty frame
	EXPECT_TRUE(px.empty());
	EXPECT_FALSE(f.decode_frame(4, px));
}

TEST(Shp, RejectsMalformedFiles)
{
	ShpFile f;
	EXPECT_FALSE(ShpFile::parse(Bytes(4), f));
	Bytes shp = build_shp({ Bytes { 1, 2, 3, 4 } }, { 0 }, 2, 2);
	shp[0] = 1; // the first word must be zero
	EXPECT_FALSE(ShpFile::parse(shp, f));
	// Truncated pixel data fails at decode time.
	Bytes cut = build_shp({ Bytes { 1, 2, 3, 4 } }, { 0 }, 2, 2);
	cut.pop_back();
	ASSERT_TRUE(ShpFile::parse(cut, f));
	std::vector<uint8_t> px;
	EXPECT_FALSE(f.decode_frame(0, px));
}

// ---- TMP ----

TEST(Tmp, ParsesTilesAndExpandsTheDiamond)
{
	Bytes v;
	put32(v, 2); put32(v, 1); put32(v, 60); put32(v, 30); // 2 x 1 slots
	const uint32_t tile0 = 16 + 8;
	put32(v, tile0);
	put32(v, 0); // second slot empty
	// Tile header (52 bytes)
	put32(v, 0); put32(v, 0);                  // x, y
	put32(v, 52 + 900 + 900);                  // extra offset
	put32(v, 52 + 900);                        // depth offset
	put32(v, 52 + 900 + 900 + 6);              // extra depth offset
	put32(v, 5); put32(v, 6);                  // extra x, y
	put32(v, 3); put32(v, 2);                  // extra width, height
	put32(v, 3);                               // flags: extra + depth
	v.push_back(2); v.push_back(9); v.push_back(4);        // height, land type, ramp
	for (int i = 0; i < 6; ++i) v.push_back(uint8_t(10 + i));      // radar colours
	v.push_back(0); v.push_back(0); v.push_back(0);        // padding to 52 bytes
	for (int i = 0; i < 900; ++i) v.push_back(uint8_t(1 + i % 200));
	for (int i = 0; i < 900; ++i) v.push_back(uint8_t(7));
	for (int i = 0; i < 6; ++i) v.push_back(uint8_t(50 + i));        // extra pixels
	for (int i = 0; i < 6; ++i) v.push_back(uint8_t(70 + i));        // extra depth

	TmpFile t;
	ASSERT_TRUE(TmpFile::parse(v, t));
	EXPECT_EQ(t.blocks_wide(), 2u);
	EXPECT_EQ(t.present_count(), 1u);
	const TmpTile& tile = t.tiles()[0];
	ASSERT_TRUE(tile.present);
	EXPECT_FALSE(t.tiles()[1].present);
	EXPECT_EQ(tile.height, 2);
	EXPECT_EQ(tile.land_type, 9);
	EXPECT_EQ(tile.ramp_type, 4);
	EXPECT_EQ(tile.radar_left.r, 10);
	EXPECT_EQ(tile.radar_right.b, 15);
	ASSERT_EQ(tile.image.size(), 900u);
	ASSERT_EQ(tile.depth.size(), 900u);
	ASSERT_EQ(tile.extra.size(), 6u);
	EXPECT_EQ(tile.extra[5], 55);
	EXPECT_EQ(tile.extra_depth[0], 70);
	EXPECT_EQ(tile.extra_x, 5);

	std::vector<uint8_t> px;
	expand_tile_diamond(tile.image, px);
	ASSERT_EQ(px.size(), 1800u);
	// Row 0 is 4 pixels wide, centred: columns 28..31 hold the first four image bytes.
	EXPECT_EQ(px[27], 0);
	EXPECT_EQ(px[28], 1);
	EXPECT_EQ(px[31], 4);
	EXPECT_EQ(px[32], 0);
	// The widest row (14) spans all 60 columns.
	EXPECT_NE(px[14 * 60], 0);
	EXPECT_NE(px[14 * 60 + 59], 0);
	// The last row is empty.
	EXPECT_EQ(px[29 * 60 + 30], 0);
	int total = 0;
	for (int y = 0; y < 30; ++y)
		total += tile_row_width(y);
	EXPECT_EQ(total, 900);
}

TEST(Tmp, RejectsWrongTileSize)
{
	Bytes v;
	put32(v, 1); put32(v, 1); put32(v, 48); put32(v, 24); put32(v, 0);
	TmpFile t;
	EXPECT_FALSE(TmpFile::parse(v, t));
}

// ---- Voxels ----

TEST(Voxel, DecodesALimbAndTheAnimationFile)
{
	// One limb of 2 x 1 x 4 voxels. Column 0 has two voxels (z = 1 and 2); column 1 is empty.
	Bytes body;
	const uint32_t span_start = 0, span_end = 8, span_data = 16;
	put32(body, 0);                                  // column 0 starts at 0
	put32(body, 0xFFFFFFFFu);                        // column 1 empty
	put32(body, 6);                                  // column 0 ends at 6 (the trailing count byte)
	put32(body, 0xFFFFFFFFu);
	// span: skip 1, count 2, {color, normal} x 2, count
	body.insert(body.end(), { 1, 2, 100, 3, 101, 4, 2 });

	Bytes v;
	puts(v, "Voxel Animation", 16);
	put32(v, 1); put32(v, 1); put32(v, 1); put32(v, uint32_t(body.size()));
	v.push_back(16); v.push_back(31);
	for (int i = 0; i < 768; ++i) v.push_back(uint8_t(i));
	puts(v, "body", 16); put32(v, 0); put32(v, 0); put32(v, 0);         // limb header (28 bytes)
	v.insert(v.end(), body.begin(), body.end());
	put32(v, span_start); put32(v, span_end); put32(v, span_data);       // limb tailer (92 bytes)
	putf(v, 0.0625f);
	for (int i = 0; i < 12; ++i) putf(v, float(i));
	for (int i = 0; i < 3; ++i) putf(v, -1.0f);
	for (int i = 0; i < 3; ++i) putf(v, 1.0f);
	v.push_back(2); v.push_back(1); v.push_back(3); v.push_back(4);

	VxlFile vxl;
	ASSERT_TRUE(VxlFile::parse(v, vxl));
	ASSERT_EQ(vxl.limbs().size(), 1u);
	EXPECT_EQ(vxl.limbs()[0].name, "body");
	EXPECT_EQ(vxl.limbs()[0].normal_mode, 4);
	EXPECT_FLOAT_EQ(vxl.limbs()[0].scale, 0.0625f);
	EXPECT_FLOAT_EQ(vxl.limbs()[0].transform[5], 5.0f);
	EXPECT_EQ(vxl.remap_start(), 16);
	std::vector<Voxel> voxels;
	ASSERT_TRUE(vxl.decode_limb(0, voxels));
	ASSERT_EQ(voxels.size(), 2u);
	EXPECT_EQ(voxels[0].z, 1);
	EXPECT_EQ(voxels[0].color, 100);
	EXPECT_EQ(voxels[1].z, 2);
	EXPECT_EQ(voxels[1].normal, 4);
	EXPECT_EQ(voxels[1].x, 0);

	Bytes hva;
	puts(hva, "body", 16);
	put32(hva, 2); put32(hva, 1);
	puts(hva, "body", 16);
	for (int i = 0; i < 24; ++i) putf(hva, float(i));
	HvaFile h;
	ASSERT_TRUE(HvaFile::parse(hva, h));
	EXPECT_EQ(h.frame_count(), 2u);
	EXPECT_EQ(h.section_names()[0], "body");
	EXPECT_FLOAT_EQ(h.matrix(1, 0)[0], 12.0f);

	Bytes vpl;
	put32(vpl, 16); put32(vpl, 31); put32(vpl, 2); put32(vpl, 0);
	vpl.resize(16 + 768, 1);
	vpl.resize(16 + 768 + 512, 3);
	VplFile p;
	ASSERT_TRUE(VplFile::parse(vpl, p));
	EXPECT_EQ(p.section_count, 2u);
	EXPECT_EQ(p.table(1)[255], 3);
}

TEST(Voxel, RejectsBadFiles)
{
	VxlFile vxl;
	EXPECT_FALSE(VxlFile::parse(Bytes(900), vxl));
	HvaFile h;
	EXPECT_FALSE(HvaFile::parse(Bytes(10), h));
}

// ---- CSF ----

TEST(Csf, ParsesLabelsStringsAndExtraData)
{
	auto wide = [](Bytes& v, const std::u16string& s) {
		for (char16_t c : s) {
			v.push_back(uint8_t(~(c & 0xFF)));
			v.push_back(uint8_t(~(c >> 8)));
		}
	};
	Bytes v;
	puts(v, " FSC"); put32(v, 3); put32(v, 2); put32(v, 2); put32(v, 0); put32(v, 0);
	puts(v, " LBL"); put32(v, 1); put32(v, 6); puts(v, "GUI:Ok");
	puts(v, " RTS"); put32(v, 2); wide(v, u"OK");
	puts(v, " LBL"); put32(v, 1); put32(v, 7); puts(v, "TXT_Hi\xE9");
	puts(v, "WRTS"); put32(v, 3); wide(v, u"Hé中"); put32(v, 4); puts(v, "hi01");

	CsfFile csf;
	ASSERT_TRUE(CsfFile::parse(v, csf));
	EXPECT_EQ(csf.label_count(), 2u);
	ASSERT_NE(csf.find("gui:ok"), nullptr);
	EXPECT_EQ(csf.find("GUI:OK")->text, u"OK");
	EXPECT_EQ(csf.text_or_label("GUI:Ok"), "OK");
	EXPECT_EQ(csf.text_or_label("Unknown:Label"), "Unknown:Label");
	const CsfString* hi = csf.find("TXT_Hi\xE9");
	ASSERT_NE(hi, nullptr);
	EXPECT_EQ(hi->extra, "hi01");
	EXPECT_EQ(utf16_to_utf8(hi->text), "H\xC3\xA9\xE4\xB8\xAD");

	v.pop_back();
	EXPECT_FALSE(CsfFile::parse(v, csf));
}

// ---- PCX ----

TEST(Pcx, DecodesRunLengthPixelsAndPalette)
{
	Bytes v(128, 0);
	v[0] = 0x0A; v[2] = 1; v[3] = 8;
	v[8] = 3; v[10] = 1;      // 4 x 2 pixels
	v[65] = 1; v[66] = 4;     // 1 plane, 4 bytes per line
	// row 0: 4 x 9 as one run; row 1: 1, 2, then a run of two 3s
	v.insert(v.end(), { 0xC4, 9, 1, 2, 0xC2, 3 });
	v.push_back(0x0C);
	for (int i = 0; i < 768; ++i) v.push_back(uint8_t(i));
	PcxImage img;
	ASSERT_TRUE(parse_pcx(v, img));
	EXPECT_EQ(img.width, 4u);
	EXPECT_EQ(img.height, 2u);
	EXPECT_EQ(img.pixels, (Bytes { 9, 9, 9, 9, 1, 2, 3, 3 }));
	EXPECT_EQ(img.palette[2].g, uint8_t(7));
	v[3] = 4;
	EXPECT_FALSE(parse_pcx(v, img));

	// 24-bit: three planes per line, each plane run-length coded within the line.
	Bytes t(128, 0);
	t[0] = 0x0A; t[2] = 1; t[3] = 8;
	t[8] = 1; t[10] = 0;       // 2 x 1 pixels
	t[65] = 3; t[66] = 2;      // 3 planes, 2 bytes per plane
	t.insert(t.end(), { 10, 20, 0xC2, 30, 40, 50 }); // R = 10,20  G = 30,30  B = 40,50
	PcxImage rgb;
	ASSERT_TRUE(parse_pcx(t, rgb));
	EXPECT_TRUE(rgb.truecolor);
	EXPECT_EQ(rgb.rgb, (Bytes { 10, 30, 40, 20, 30, 50 }));
}

TEST(Mix, DetectsOldFormatArchivesByTheirSize)
{
	// u16 count = 1, u32 body size = 3, one 12-byte entry, then 3 body bytes.
	Bytes v;
	put16(v, 1); put32(v, 3);
	put32(v, 0x12345678); put32(v, 0); put32(v, 3);
	v.insert(v.end(), { 1, 2, 3 });
	EXPECT_TRUE(ra::vfs::MixArchive::looks_like_archive(v));
	v.push_back(0);
	EXPECT_FALSE(ra::vfs::MixArchive::looks_like_archive(v));
}

// ---- Fonts ----

TEST(Fnt, DirectAndRangedGlyphLookup)
{
	// "fonT": 65536-entry table; glyph record size = 1 + bytes_per_row * height = 1 + 1*2.
	Bytes v;
	puts(v, "fonT");
	put32(v, 8); put32(v, 1); put32(v, 2); put32(v, 3); put32(v, 2); put32(v, 3);
	Bytes table(0x20000, 0);
	auto set = [&](unsigned cp, unsigned glyph) { table[cp * 2] = uint8_t(glyph); table[cp * 2 + 1] = uint8_t(glyph >> 8); };
	set('A', 1);
	set('B', 2);
	v.insert(v.end(), table.begin(), table.end());
	v.insert(v.end(), { 5, 0xAA, 0xBB, 6, 0xCC, 0xDD });
	FntFile f;
	ASSERT_TRUE(FntFile::parse(v, f));
	EXPECT_EQ(f.height(), 2u);
	ASSERT_EQ(f.glyph('B').size(), 3u);
	EXPECT_EQ(f.glyph('B')[0], 6);
	EXPECT_EQ(f.glyph('A')[1], 0xAA);
	EXPECT_TRUE(f.glyph('C').empty());
	EXPECT_TRUE(f.glyph(0x20000).empty());

	Bytes r;
	puts(r, "FoNt");
	put32(r, 8); put32(r, 1); put32(r, 2); put32(r, 3); put32(r, 1); put32(r, 3);
	put32(r, 36);                      // range table offset
	put32(r, 36 + 12);                 // glyph data offset
	put32(r, 0); put32(r, 'a'); put32(r, 'b');   // glyph numbers start at 1 for 'a'..'b'
	r.insert(r.end(), { 4, 1, 2, 7, 3, 4 });
	FntFile g;
	ASSERT_TRUE(FntFile::parse(r, g));
	EXPECT_EQ(g.glyph_count(), 2u);
	EXPECT_EQ(g.glyph('b')[0], 7);
	EXPECT_TRUE(g.glyph('c').empty());
}

// ---- Audio ----

TEST(Audio, ImaAdpcmBlockDecoding)
{
	// Mono block of 4 + 4 bytes: header predictor 1000, index 0, then 8 samples of nibble 0.
	// Nibble 0 adds step/8 each time (step 7 -> diff 0), so the output stays at 1000 while
	// the step index falls by one each sample (clamped at 0).
	Bytes block;
	put16(block, 1000);
	block.push_back(0);
	block.push_back(0);
	block.insert(block.end(), { 0, 0, 0, 0 });
	std::vector<int16_t> out;
	ASSERT_TRUE(decode_ima_adpcm_blocks(block, 1, 8, out));
	ASSERT_EQ(out.size(), 9u);
	for (int16_t s : out)
		EXPECT_EQ(s, 1000);

	// Nibble 7 (+step*1.. ) raises the sample; nibble 15 lowers it.
	Bytes up;
	put16(up, 0);
	up.push_back(0);
	up.push_back(0);
	up.insert(up.end(), { 0x77, 0x77, 0x77, 0x77 });
	ASSERT_TRUE(decode_ima_adpcm_blocks(up, 1, 8, out));
	EXPECT_GT(out[8], out[1]);
	EXPECT_GT(out[1], out[0]);
	Bytes down;
	put16(down, 0);
	down.push_back(0);
	down.push_back(0);
	down.insert(down.end(), { 0xFF, 0xFF, 0xFF, 0xFF });
	ASSERT_TRUE(decode_ima_adpcm_blocks(down, 1, 8, out));
	EXPECT_LT(out[8], out[0]);
}

TEST(Audio, WavHeaderAndStereoAdpcm)
{
	// A stereo IMA ADPCM file with one 16-byte block: two 4-byte headers, then 4 bytes per channel.
	Bytes wav;
	puts(wav, "RIFF"); put32(wav, 0); puts(wav, "WAVE");
	puts(wav, "fmt "); put32(wav, 20);
	put16(wav, 0x11); put16(wav, 2); put32(wav, 22050); put32(wav, 22050 * 16 / 9); put16(wav, 16); put16(wav, 4);
	put16(wav, 2); put16(wav, 9);
	puts(wav, "data"); put32(wav, 16);
	put16(wav, 100); wav.push_back(0); wav.push_back(0);      // left header
	put16(wav, uint32_t(-100) & 0xFFFF); wav.push_back(0); wav.push_back(0); // right header
	wav.insert(wav.end(), { 0, 0, 0, 0, 0, 0, 0, 0 });
	PcmAudio pcm;
	ASSERT_TRUE(decode_wav(wav, pcm));
	EXPECT_EQ(pcm.channels, 2);
	EXPECT_EQ(pcm.sample_rate, 22050u);
	ASSERT_EQ(pcm.samples.size(), 9u * 2);
	EXPECT_EQ(pcm.samples[0], 100);
	EXPECT_EQ(pcm.samples[1], -100);
	EXPECT_EQ(pcm.samples[2 * 8], 100);
	EXPECT_EQ(pcm.samples[2 * 8 + 1], -100);

	WavInfo info;
	ASSERT_TRUE(parse_wav_header(wav, info));
	EXPECT_EQ(info.format, 0x11);
	EXPECT_EQ(info.data.size(), 16u);
	EXPECT_FALSE(parse_wav_header(Bytes(40), info));
}

TEST(Audio, IndexParsingAndBankBounds)
{
	Bytes idx;
	puts(idx, "GABA"); put32(idx, 2); put32(idx, 2);
	puts(idx, "boom", 16); put32(idx, 0); put32(idx, 100); put32(idx, 22050); put32(idx, 12); put32(idx, 512);
	puts(idx, "zap", 16); put32(idx, 100); put32(idx, 50); put32(idx, 22050); put32(idx, 6); put32(idx, 0);
	AudioIndex index;
	ASSERT_TRUE(AudioIndex::parse(idx, index));
	ASSERT_EQ(index.entries.size(), 2u);
	EXPECT_EQ(index.entries[1].name, "zap");
	EXPECT_EQ(index.entries[0].flags, 12u);
	EXPECT_TRUE(index.fits_bank(150));
	EXPECT_FALSE(index.fits_bank(149));
}

// ---- Maps ----

TEST(Map, UnpacksCellsOverlaysAndPreview)
{
	const std::string text = read_file("test_map.map");
	ASSERT_FALSE(text.empty()) << "missing tests/data/test_map.map";
	MapData map;
	std::string error;
	ASSERT_TRUE(load_map_text(text, map, &error)) << error;

	EXPECT_EQ(map.theater, "TEMPERATE");
	EXPECT_EQ(map.map_width, 60);
	EXPECT_EQ(map.local_x, 2);
	EXPECT_EQ(map.local_height, 50);
	EXPECT_EQ(map.ini.get_string("Basic", "Name"), "Test Map");
	EXPECT_EQ(map.ini.get_string("Waypoints", "1"), "2020"); // the trailing comment is stripped

	ASSERT_EQ(map.cells.size(), 4u);
	EXPECT_EQ(map.cells[0].x, 10);
	EXPECT_EQ(map.cells[0].y, 20);
	EXPECT_EQ(map.cells[0].tile, 5);
	EXPECT_EQ(map.cells[1].sub_tile, 1);
	EXPECT_EQ(map.cells[2].tile, 0xFFFF);
	EXPECT_EQ(map.cells[3].tile, 123456);
	EXPECT_EQ(map.cells[3].height, 14);

	ASSERT_EQ(map.overlay.size(), size_t(512 * 512));
	ASSERT_EQ(map.overlay_data.size(), size_t(512 * 512));
	EXPECT_EQ(map.overlay[6 * 512 + 5], 24);
	EXPECT_EQ(map.overlay_data[6 * 512 + 5], 3);
	EXPECT_EQ(map.overlay[300 * 512 + 200], 1);
	EXPECT_EQ(map.overlay_data[300 * 512 + 200], 15);
	EXPECT_EQ(map.overlay[511 * 512 + 511], 7);
	EXPECT_EQ(map.overlay[0], kNoOverlay);

	EXPECT_EQ(map.preview.width, 6u);
	EXPECT_EQ(map.preview.height, 4u);
	ASSERT_EQ(map.preview.rgb.size(), 6u * 4 * 3);
	EXPECT_EQ(map.preview.rgb[10], uint8_t(70));
}

TEST(Map, ReportsCorruptPackedSections)
{
	MapData map;
	std::string error;
	EXPECT_FALSE(load_map_text("[IsoMapPack5]\n1=//8BAAE=\n", map, &error));
	EXPECT_NE(error.find("IsoMapPack5"), std::string::npos);
	EXPECT_FALSE(load_map_text("[PreviewPack]\n1=AAAA\n", map, &error)); // no [Preview] Size
	EXPECT_TRUE(load_map_text("[Basic]\nName=x\n", map, &error));       // nothing packed is fine
}
