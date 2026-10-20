#include "formats/compression.h"

#include "test_data.h"

#include <gtest/gtest.h>

using namespace ra::formats;

namespace {

std::vector<uint8_t> unhex(const std::string& hex)
{
	std::vector<uint8_t> out;
	if (hex == "-")
		return out;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	return out;
}

} // namespace

TEST(Lzo1x, MatchesCompressorOutput)
{
	const auto rows = ra::test::read_data_file("lzo_vectors.txt");
	ASSERT_GT(rows.size(), 20u);
	for (const auto& r : rows) {
		const auto packed = unhex(r[0]);
		const auto original = unhex(r.size() > 1 ? r[1] : "-");
		std::vector<uint8_t> out;
		ASSERT_TRUE(lzo1x_decompress(packed, out)) << "original size " << original.size();
		EXPECT_EQ(out, original);
	}
}

TEST(Lzo1x, RejectsTruncatedAndOversizedData)
{
	const auto rows = ra::test::read_data_file("lzo_vectors.txt");
	const auto packed = unhex(rows[rows.size() / 2][0]);
	ASSERT_GT(packed.size(), 20u);
	std::vector<uint8_t> out;
	// Cutting the stream short must fail cleanly, never read out of bounds.
	for (size_t cut : { size_t(1), packed.size() / 2, packed.size() - 1 })
		EXPECT_FALSE(lzo1x_decompress(std::span<const uint8_t>(packed.data(), cut), out));
	// A size limit smaller than the real output is an error.
	out.clear();
	const auto original = unhex(rows[rows.size() / 2][1]);
	EXPECT_FALSE(lzo1x_decompress(packed, out, original.size() / 2));
}

TEST(Lzo1x, SurvivesRandomGarbage)
{
	uint32_t seed = 12345;
	for (int round = 0; round < 500; ++round) {
		std::vector<uint8_t> junk(1 + round % 200);
		for (auto& b : junk) {
			seed = seed * 1664525u + 1013904223u;
			b = static_cast<uint8_t>(seed >> 24);
		}
		std::vector<uint8_t> out;
		(void)lzo1x_decompress(junk, out, 1 << 16); // may succeed or fail, must not crash
	}
}

TEST(Lcw, LiteralsFillsAndCopies)
{
	std::vector<uint8_t> stream = {
		0x85, 'h', 'e', 'l', 'l', 'o',        // 5 literal bytes
		0xFE, 0x03, 0x00, '!',               // fill: three '!'
		0x00 | (1 << 4), 0x08,               // relative copy: 4 bytes from 8 back
		0xC0 | 0x00, 0x00, 0x00,             // absolute copy: 3 bytes from output offset 0
		0xFF, 0x02, 0x00, 0x01, 0x00,        // absolute copy: 2 bytes from offset 1
		0x80,
	};
	std::vector<uint8_t> out;
	ASSERT_TRUE(lcw_decompress(stream, out));
	const std::string text(out.begin(), out.end());
	EXPECT_EQ(text.substr(0, 8), "hello!!!");
	// the relative copy takes 4 bytes starting 8 back from the end of "hello!!!", which is "hell"
	EXPECT_EQ(text.substr(8, 4), "hell");
	EXPECT_EQ(text.substr(12, 3), "hel"); // absolute copy from offset 0
	EXPECT_EQ(text.substr(15, 2), "el");  // absolute copy from offset 1
	EXPECT_EQ(text.size(), 17u);
}

TEST(Lcw, StopsAtEndMarkerAndAppends)
{
	const std::vector<uint8_t> stream = { 0x82, 'a', 'b', 0x80, 0x82, 'x', 'y' };
	std::vector<uint8_t> out = { 'Z' };
	ASSERT_TRUE(lcw_decompress(stream, out));
	EXPECT_EQ(std::string(out.begin(), out.end()), "Zab");
}

TEST(Lcw, RejectsBadCopies)
{
	std::vector<uint8_t> out;
	// Relative copy reaching before the start of the output.
	EXPECT_FALSE(lcw_decompress(std::vector<uint8_t> { 0x81, 'a', 0x10, 0x09, 0x80 }, out));
	out.clear();
	// Truncated literal run.
	EXPECT_FALSE(lcw_decompress(std::vector<uint8_t> { 0x85, 'a', 'b' }, out));
	out.clear();
	// Output limit.
	EXPECT_FALSE(lcw_decompress(std::vector<uint8_t> { 0xFE, 0xFF, 0xFF, 0x00, 0x80 }, out, 1000));
}

TEST(Chunks, ConcatenatesDecodedChunks)
{
	// Two LCW chunks: "ab" and "cdef".
	const std::vector<uint8_t> packed = {
		4, 0, 2, 0, 0x82, 'a', 'b', 0x80,
		6, 0, 4, 0, 0x84, 'c', 'd', 'e', 'f', 0x80,
	};
	std::vector<uint8_t> out;
	ASSERT_TRUE(decode_chunks(packed, ChunkCodec::Lcw, out));
	EXPECT_EQ(std::string(out.begin(), out.end()), "abcdef");
	// A chunk header promising more bytes than exist.
	const std::vector<uint8_t> broken = { 9, 0, 2, 0, 0x82, 'a' };
	EXPECT_FALSE(decode_chunks(broken, ChunkCodec::Lcw, out));
}
