#include "vfs/blowfish.h"
#include "vfs/byte_source.h"
#include "vfs/mix_archive.h"
#include "vfs/mix_id.h"
#include "vfs/mix_key.h"
#include "vfs/vfs.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace ra::vfs;

namespace {

std::vector<uint8_t> unhex(const std::string& hex)
{
	std::vector<uint8_t> out;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	return out;
}

void put32(std::vector<uint8_t>& v, uint32_t x)
{
	for (int i = 0; i < 4; ++i)
		v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void put16(std::vector<uint8_t>& v, uint16_t x)
{
	v.push_back(static_cast<uint8_t>(x));
	v.push_back(static_cast<uint8_t>(x >> 8));
}

struct TestFile {
	std::string name;
	std::vector<uint8_t> bytes;
};

// Builds an unencrypted new-format archive.
std::vector<uint8_t> build_mix(const std::vector<TestFile>& files, bool old_format = false)
{
	struct Item {
		uint32_t id;
		const TestFile* file;
		uint32_t offset;
	};
	std::vector<Item> items;
	for (const auto& f : files)
		items.push_back({ mix_id(f.name), &f, 0 });
	std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return int32_t(a.id) < int32_t(b.id); });
	uint32_t offset = 0;
	for (auto& it : items) {
		it.offset = offset;
		offset += static_cast<uint32_t>(it.file->bytes.size());
	}
	std::vector<uint8_t> out;
	if (!old_format)
		put32(out, 0);
	put16(out, static_cast<uint16_t>(items.size()));
	put32(out, offset);
	for (const auto& it : items) {
		put32(out, it.id);
		put32(out, it.offset);
		put32(out, static_cast<uint32_t>(it.file->bytes.size()));
	}
	for (const auto& it : items)
		out.insert(out.end(), it.file->bytes.begin(), it.file->bytes.end());
	return out;
}

std::shared_ptr<const ByteSource> memory(std::vector<uint8_t> bytes)
{
	return std::make_shared<MemorySource>(std::move(bytes));
}

} // namespace

TEST(MixId, MatchesReferenceImplementation)
{
	int checked = 0;
	for (const auto& row : ra::test::read_data_file("vfs_vectors.txt")) {
		if (row[0] != "id")
			continue;
		std::string name = row[1];
		std::replace(name.begin(), name.end(), '_', ' ');
		EXPECT_EQ(mix_id(name), static_cast<uint32_t>(std::stoul(row[2], nullptr, 16))) << name;
		++checked;
	}
	EXPECT_GT(checked, 20);
}

TEST(MixId, IsCaseInsensitive)
{
	EXPECT_EQ(mix_id("rulesmd.ini"), mix_id("RULESMD.INI"));
	EXPECT_EQ(mix_id("Local.Mix"), mix_id("LOCAL.MIX"));
	EXPECT_NE(mix_id("a.shp"), mix_id("b.shp"));
}

TEST(Blowfish, MatchesReferenceImplementation)
{
	int checked = 0;
	for (const auto& row : ra::test::read_data_file("vfs_vectors.txt")) {
		if (row[0] != "blowfish")
			continue;
		const auto key = unhex(row[1]);
		auto block = unhex(row[2]);
		const auto cipher = unhex(row[3]);
		const Blowfish bf(key.data(), key.size());
		auto enc = block;
		bf.encrypt_block(enc.data());
		EXPECT_EQ(enc, cipher) << "key " << row[1];
		bf.decrypt_block(enc.data());
		EXPECT_EQ(enc, block);
		++checked;
	}
	EXPECT_GE(checked, 40);
}

TEST(Blowfish, KnownAnswerAllZero)
{
	// The all-zero key and block are a standard Blowfish test vector.
	const uint8_t key[8] = {};
	uint8_t block[8] = {};
	Blowfish(key, 8).encrypt_block(block);
	const uint8_t expected[8] = { 0x4E, 0xF9, 0x97, 0x45, 0x61, 0x98, 0xDD, 0x78 };
	EXPECT_EQ(std::memcmp(block, expected, 8), 0);
}

TEST(MixKey, ModularExponentiationMatchesReference)
{
	int checked = 0;
	for (const auto& row : ra::test::read_data_file("vfs_vectors.txt")) {
		if (row[0] != "modpow")
			continue;
		const auto base = unhex(row[1]);
		const auto modulus = unhex(row[3]);
		const auto expected = unhex(row[4]);
		std::vector<uint8_t> out(expected.size());
		mod_pow_le(base.data(), static_cast<int>(base.size()), static_cast<uint32_t>(std::stoul(row[2])), modulus.data(), static_cast<int>(modulus.size()), out.data(), static_cast<int>(out.size()));
		EXPECT_EQ(out, expected) << "base " << row[1];
		++checked;
	}
	EXPECT_GE(checked, 15);
}

TEST(MixArchive, ReadsNewFormat)
{
	const std::vector<TestFile> files = { { "a.txt", { 1, 2, 3 } }, { "b.bin", { 9 } }, { "empty.dat", {} }, { "Long.Name.INI", { 5, 6, 7, 8, 9, 10 } } };
	std::string error;
	auto mix = MixArchive::open(memory(build_mix(files)), &error);
	ASSERT_TRUE(mix) << error;
	EXPECT_EQ(mix->entries().size(), 4u);
	EXPECT_FALSE(mix->encrypted());
	for (const auto& f : files) {
		const MixEntry* e = mix->find(f.name);
		ASSERT_NE(e, nullptr) << f.name;
		const auto data = mix->data(*e);
		EXPECT_EQ(std::vector<uint8_t>(data.begin(), data.end()), f.bytes) << f.name;
	}
	EXPECT_EQ(mix->find("missing.txt"), nullptr);
	EXPECT_NE(mix->find("LONG.NAME.INI"), nullptr);
}

TEST(MixArchive, ReadsOldFormat)
{
	const std::vector<TestFile> files = { { "x.vqa", { 1, 2 } }, { "y.aud", { 3, 4, 5 } } };
	auto bytes = build_mix(files, true);
	std::string error;
	auto mix = MixArchive::open(memory(bytes), &error);
	ASSERT_TRUE(mix) << error;
	EXPECT_TRUE(mix->old_format());
	const MixEntry* e = mix->find("y.aud");
	ASSERT_NE(e, nullptr);
	EXPECT_EQ(mix->data(*e).size(), 3u);
}

TEST(MixArchive, SortsBySignedId)
{
	// Names chosen at random; the table must be in signed-id order after opening.
	std::vector<TestFile> files;
	for (int i = 0; i < 50; ++i)
		files.push_back({ "file" + std::to_string(i) + ".dat", { static_cast<uint8_t>(i) } });
	auto mix = MixArchive::open(memory(build_mix(files)));
	ASSERT_TRUE(mix);
	for (size_t i = 1; i < mix->entries().size(); ++i)
		EXPECT_LT(int32_t(mix->entries()[i - 1].id), int32_t(mix->entries()[i].id));
	for (const auto& f : files)
		EXPECT_EQ(mix->data(*mix->find(f.name))[0], f.bytes[0]);
}

TEST(MixArchive, OpensNestedArchives)
{
	const auto inner = build_mix({ { "deep.txt", { 42, 43 } } });
	const auto outer = build_mix({ { "inner.mix", inner }, { "side.txt", { 1 } } });
	auto mix = MixArchive::open(memory(outer));
	ASSERT_TRUE(mix);
	const MixEntry* e = mix->find("inner.mix");
	ASSERT_NE(e, nullptr);
	EXPECT_TRUE(MixArchive::looks_like_archive(mix->data(*e)));
	std::string error;
	auto nested = mix->open_nested(*e, &error);
	ASSERT_TRUE(nested) << error;
	const MixEntry* deep = nested->find("deep.txt");
	ASSERT_NE(deep, nullptr);
	EXPECT_EQ(nested->data(*deep)[1], 43);
}

TEST(MixArchive, RejectsBrokenData)
{
	std::string error;
	EXPECT_FALSE(MixArchive::open(memory({ 0, 0, 0 }), &error));
	// An entry that points outside the archive.
	auto bytes = build_mix({ { "a.txt", { 1, 2, 3 } } });
	bytes.resize(bytes.size() - 2);
	EXPECT_FALSE(MixArchive::open(memory(bytes), &error));
	EXPECT_FALSE(error.empty());
	// A table cut short.
	auto cut = build_mix({ { "a.txt", { 1 } }, { "b.txt", { 2 } } });
	cut.resize(14);
	EXPECT_FALSE(MixArchive::open(memory(cut), &error));
}

TEST(Vfs, LooseFilesOverrideArchivesAndNamesAreCaseInsensitive)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "re2yr_vfs_test";
	fs::remove_all(dir);
	fs::create_directories(dir);
	{
		std::ofstream(dir / "Rules.INI", std::ios::binary) << "loose";
		const auto mix = build_mix({ { "rules.ini", { 'a', 'r', 'c' } }, { "other.txt", { 'o' } } });
		std::ofstream out(dir / "Pack.mix", std::ios::binary);
		out.write(reinterpret_cast<const char*>(mix.data()), static_cast<std::streamsize>(mix.size()));
	}

	{ // the Vfs and its mapped archive must be gone before the folder can be deleted
		Vfs vfs;
		vfs.set_loose_directory(dir.string());
		std::string error;
		ASSERT_TRUE(vfs.mount("pack.mix", &error)) << error;
		EXPECT_EQ(vfs.mounts().size(), 1u);

		auto rules = vfs.open("RULES.ini");
		ASSERT_TRUE(rules);
		EXPECT_EQ(rules.text(), "loose"); // the loose file wins
		auto other = vfs.open("OTHER.TXT");
		ASSERT_TRUE(other);
		EXPECT_EQ(other.text(), "o");
		EXPECT_FALSE(vfs.exists("nothing.here"));
		EXPECT_TRUE(vfs.exists("other.txt"));
		EXPECT_TRUE(vfs.mount("pack.mix")); // mounting twice is harmless
		EXPECT_EQ(vfs.mounts().size(), 1u);
	}

	fs::remove_all(dir);
}

TEST(Vfs, FirstMountedArchiveWins)
{
	const auto first = build_mix({ { "x.ini", { '1' } } });
	const auto second = build_mix({ { "x.ini", { '2' } }, { "y.ini", { '3' } } });
	Vfs vfs;
	ASSERT_TRUE(vfs.mount_archive("first.mix", MixArchive::open(memory(first))));
	ASSERT_TRUE(vfs.mount_archive("second.mix", MixArchive::open(memory(second))));
	EXPECT_EQ(vfs.open("x.ini").text(), "1");
	EXPECT_EQ(vfs.open("y.ini").text(), "3");
}

TEST(Vfs, MountsNestedArchivesByName)
{
	const auto inner = build_mix({ { "rules.ini", { 'i' } } });
	const auto outer = build_mix({ { "local.mix", inner } });
	Vfs vfs;
	vfs.mount_archive("ra2.mix", MixArchive::open(memory(outer)));
	EXPECT_FALSE(vfs.exists("rules.ini"));
	ASSERT_TRUE(vfs.mount("local.mix"));
	EXPECT_EQ(vfs.mounts().back().label, "ra2.mix/local.mix");
	EXPECT_EQ(vfs.open("rules.ini").text(), "i");
	EXPECT_FALSE(vfs.mount("nope.mix"));
}

TEST(Vfs, VisitAllWalksNestedArchivesAndUsesNames)
{
	const auto inner = build_mix({ { "deep.txt", { 1 } } });
	const auto outer = build_mix({ { "inner.mix", inner }, { "side.txt", { 2 } } });
	Vfs vfs;
	vfs.mount_archive("outer.mix", MixArchive::open(memory(outer)));
	NameDb names;
	names.add("deep.txt");
	names.add("inner.mix");
	std::vector<std::string> paths;
	vfs.visit_all(&names, [&](const Vfs::VisitedEntry& e) { paths.push_back(e.path); });
	ASSERT_EQ(paths.size(), 2u);
	// The nested archive is walked rather than reported, and unnamed files show their hex id.
	EXPECT_NE(std::find(paths.begin(), paths.end(), "outer.mix/inner.mix/deep.txt"), paths.end());
	char hex[16];
	std::snprintf(hex, sizeof hex, "%08X", mix_id("side.txt"));
	EXPECT_NE(std::find(paths.begin(), paths.end(), "outer.mix/" + std::string(hex)), paths.end());
}

TEST(NameDb, AddsLinesAndFindsById)
{
	NameDb db;
	EXPECT_EQ(db.add_lines("rules.ini\r\n; comment\r\n\r\nart.ini\n"), 2u);
	ASSERT_NE(db.find(mix_id("RULES.INI")), nullptr);
	EXPECT_EQ(*db.find(mix_id("ART.INI")), "art.ini");
	EXPECT_EQ(db.find(1234), nullptr);
}
