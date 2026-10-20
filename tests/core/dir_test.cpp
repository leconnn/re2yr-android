#include "core/dir.h"

#include <gtest/gtest.h>

using ra::Dir16;

TEST(Dir16, CompassFacings)
{
	EXPECT_EQ(Dir16(0x0000).facing<8>(), 0u); // north
	EXPECT_EQ(Dir16(0x4000).facing<8>(), 2u); // east
	EXPECT_EQ(Dir16(0x8000).facing<8>(), 4u); // south
	EXPECT_EQ(Dir16(0xC000).facing<8>(), 6u); // west
}

TEST(Dir16, RoundsToNearestAndWraps)
{
	// Half a step below north rounds up to north (step 0 after wrapping).
	EXPECT_EQ(Dir16(0xF000).facing<8>(), 0u);
	EXPECT_EQ(Dir16(0xEFFF).facing<8>(), 7u);
	EXPECT_EQ(Dir16(0x1000).facing<8>(), 1u);
	EXPECT_EQ(Dir16(0x0FFF).facing<8>(), 0u);
	EXPECT_EQ(Dir16(0x0800).facing<32>(), 1u);
}

TEST(Dir16, ByteMatchesChecksumFormula)
{
	// The sync checksum uses ((raw >> 7) + 1) >> 1, kept to 8 bits.
	for (uint32_t raw = 0; raw < 0x10000; raw += 37) {
		const auto expected = static_cast<uint8_t>((((raw >> 7) + 1) >> 1) & 0xFF);
		ASSERT_EQ(Dir16(static_cast<uint16_t>(raw)).byte(), expected) << raw;
	}
}

TEST(Dir16, SetFacing)
{
	Dir16 d;
	d.set_facing<8>(3);
	EXPECT_EQ(d.raw(), 0x6000);
	d.set_facing<32>(31);
	EXPECT_EQ(d.raw(), 0xF800);
	EXPECT_EQ(d.facing<32>(), 31u);
}
