#include "sim/world/land.h"
#include "sim/world/map_grid.h"
#include "sim/world/shroud.h"
#include "sim/world/theater_tiles.h"
#include "sim/world/world.h"

#include <gtest/gtest.h>

using namespace ra::world;

TEST(Land, TileLandBytesTranslateLikeTheOriginalTable)
{
	EXPECT_EQ(land_type_from_tile(0), LandType::Clear);
	EXPECT_EQ(land_type_from_tile(1), LandType::Ice);
	EXPECT_EQ(land_type_from_tile(5), LandType::Tunnel);
	EXPECT_EQ(land_type_from_tile(6), LandType::Railroad);
	EXPECT_EQ(land_type_from_tile(7), LandType::Rock);
	EXPECT_EQ(land_type_from_tile(9), LandType::Water);
	EXPECT_EQ(land_type_from_tile(10), LandType::Beach);
	EXPECT_EQ(land_type_from_tile(11), LandType::Road);
	EXPECT_EQ(land_type_from_tile(13), LandType::Clear);
	EXPECT_EQ(land_type_from_tile(14), LandType::Rough);
	EXPECT_EQ(land_type_from_tile(15), LandType::Rock);
	EXPECT_EQ(land_type_from_tile(200), LandType::Clear);
	EXPECT_EQ(land_type_from_name("rock"), LandType::Rock);
	EXPECT_EQ(land_type_from_name("Tiberium"), LandType::Tiberium);
	EXPECT_EQ(land_type_from_name("nonsense"), LandType::None);
}

TEST(Land, HeightLevelsAreFifteenPixelsEach)
{
	for (int level = 0; level <= 14; ++level)
		EXPECT_EQ(height_to_pixels(level * kLevelHeight), level * kPixelsPerLevel) << "level " << level;
	EXPECT_NEAR(height_scale(), 0.14352, 0.0002);
}

TEST(Land, FloorHeightFollowsTheRampPlanes)
{
	// A flat cell at level 3.
	EXPECT_EQ(floor_height(3, 0, 0, 0), 3 * 104);
	EXPECT_EQ(floor_height(3, 0, 200, 90), 3 * 104);
	// Slope 1 rises toward +x: height = x * 104 / 256 above the level.
	EXPECT_EQ(floor_height(0, 1, 0, 100), 0);
	EXPECT_EQ(floor_height(0, 1, 128, 100), 52);
	EXPECT_EQ(floor_height(0, 1, 255, 100), 103);
	// Slope 3 rises toward -x.
	EXPECT_EQ(floor_height(0, 3, 0, 0), 104);
	EXPECT_EQ(floor_height(0, 3, 128, 0), 52);
	// Slope 5, an outer corner: A = B = 1, C = -104, clamped to [0, 104].
	EXPECT_EQ(floor_height(0, 5, 0, 0), 0);
	EXPECT_EQ(floor_height(0, 5, 255, 255), 104 * 2 * 255 / 256 - 104);
	// Slope 17 is a constant 52.
	EXPECT_EQ(floor_height(2, 17, 10, 10), 2 * 104 + 52);
}

TEST(MapGrid, DiamondAndOffMapCells)
{
	MapGrid grid;
	grid.set_size(110, 110, 2, 4, 106, 104);
	EXPECT_TRUE(grid.in_map(104, 19));
	EXPECT_TRUE(grid.in_map(110, 110));
	EXPECT_FALSE(grid.in_map(5, 5));        // x + y <= W
	EXPECT_FALSE(grid.in_map(200, 5));      // x - y >= W
	EXPECT_FALSE(grid.in_map(5, 200));      // y - x >= W
	EXPECT_FALSE(grid.in_map(200, 200));    // x + y > W + 2H
	EXPECT_FALSE(grid.in_local(5, 5));

	// Off-grid reads give a blank cell instead of crashing.
	EXPECT_FALSE(grid.at(-1, 0).exists);
	EXPECT_FALSE(grid.at(600, 600).exists);
	EXPECT_EQ(grid.mutable_at(512, 0), nullptr);

	MapCell* c = grid.mutable_at(10, 20);
	ASSERT_NE(c, nullptr);
	c->level = 2;
	c->slope = 1;
	EXPECT_EQ(grid.at(10, 20).level, 2);
	// A point in the middle of that cell: level 2 plus half a ramp.
	EXPECT_EQ(grid.ground_height({ 10 * 256 + 128, 20 * 256 + 50, 0 }), 2 * 104 + 52);
}

TEST(Shroud, RevealUsesTheOriginalsCircleTable)
{
	auto count = [](int radius) {
		ShroudMap shroud;
		shroud.reveal_area(256, 256, radius);
		int n = 0;
		for (int y = 240; y < 272; ++y)
			for (int x = 240; x < 272; ++x)
				n += shroud.revealed(x, y);
		return n;
	};
	// N[r] = 1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309, 369: the table is not a perfect circle (r = 2 has 21 cells, not 25).
	const int expected[] = { 1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309, 369 };
	for (int r = 0; r <= 10; ++r)
		EXPECT_EQ(count(r), expected[r]) << "radius " << r;
	EXPECT_EQ(count(15), expected[10]); // the radius is clamped to 10
}

TEST(Shroud, StartsHiddenAndKeepsToTheGrid)
{
	ShroudMap shroud;
	EXPECT_FALSE(shroud.revealed(10, 10));
	shroud.reveal_area(0, 0, 3); // partly off the grid
	EXPECT_TRUE(shroud.revealed(0, 0));
	EXPECT_TRUE(shroud.revealed(3, 0));
	EXPECT_FALSE(shroud.revealed(-1, 0));
	shroud.reveal_all();
	EXPECT_TRUE(shroud.revealed(511, 511));
	shroud.hide_all();
	EXPECT_FALSE(shroud.revealed(511, 511));
}
