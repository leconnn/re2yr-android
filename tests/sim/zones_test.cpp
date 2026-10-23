#include "sim/path/zones.h"

#include <gtest/gtest.h>

using namespace ra::sim::path;

namespace {

constexpr int kNormal = 0, kAmphibious = 5, kWater = 10;

// A 10 x 10 map (square of 21 x 21 cells): land in [4, 16] x [4, 16], everything else outside the
// usable area; 'water_column' >= 0 puts a column of water across the land.
Zones make(int water_column, int cliff_row = -1)
{
	Zones z;
	z.build(10, 10,
		[=](int x, int y) {
			if (x < 4 || x > 16 || y < 4 || y > 16)
				return 7;
			return x == water_column ? 4 : 0;
		},
		[=](int, int y) { return cliff_row >= 0 && y >= cliff_row ? 4 : 0; });
	return z;
}

} // namespace

TEST(Zones, WaterSplitsLandZonesButNotAmphibiousOnes)
{
	const Zones open = make(-1);
	EXPECT_EQ(open.movement_zone({ 5, 5 }, kNormal), open.movement_zone({ 15, 15 }, kNormal));
	EXPECT_EQ(open.movement_zone({ 0, 0 }, kNormal), 0xFFFF); // class 7 cells have region 0

	const Zones z = make(10);
	EXPECT_NE(z.region({ 5, 5 }), z.region({ 10, 5 }));
	EXPECT_NE(z.movement_zone({ 5, 5 }, kNormal), z.movement_zone({ 15, 5 }, kNormal));
	EXPECT_EQ(z.movement_zone({ 5, 5 }, kAmphibious), z.movement_zone({ 15, 5 }, kAmphibious));
	EXPECT_EQ(z.movement_zone({ 10, 8 }, kNormal), 1); // water: a region a tank's movement zone can't cross
	EXPECT_EQ(z.movement_zone({ 10, 8 }, kWater), z.movement_zone({ 10, 12 }, kWater));
	EXPECT_NE(z.movement_zone({ 5, 8 }, kWater), z.movement_zone({ 10, 8 }, kWater));
}

TEST(Zones, ACliffMakesTwoRegions)
{
	// Level 0 above row 10, level 4 from row 10: the regions don't connect (steps of 2 or more).
	const Zones z = make(-1, 10);
	EXPECT_NE(z.region({ 8, 8 }), z.region({ 8, 12 }));
	EXPECT_NE(z.movement_zone({ 8, 8 }, kNormal), z.movement_zone({ 8, 12 }, kNormal));
}

TEST(Zones, HierarchicalPartsFollowTheBlocks)
{
	const Zones z = make(-1);
	// Level 0 cuts into 2 x 2 blocks, level 1 into 4 x 4, level 2 into 8 x 8.
	EXPECT_EQ(z.part({ 4, 4 }, 0), z.part({ 5, 5 }, 0));
	EXPECT_NE(z.part({ 5, 5 }, 0), z.part({ 6, 5 }, 0));
	EXPECT_EQ(z.part({ 4, 4 }, 1), z.part({ 7, 7 }, 1));
	EXPECT_NE(z.part({ 7, 7 }, 1), z.part({ 8, 7 }, 1));
	EXPECT_EQ(z.part({ 8, 8 }, 2), z.part({ 15, 15 }, 2));
	// Each part knows the part one level up that holds it.
	const int p0 = z.part({ 6, 6 }, 0), p1 = z.part({ 6, 6 }, 1), p2 = z.part({ 6, 6 }, 2);
	EXPECT_EQ(z.nodes(0)[size_t(p0)].parent, p1);
	EXPECT_EQ(z.nodes(1)[size_t(p1)].parent, p2);
	// Neighbouring parts are linked both ways.
	const int right = z.part({ 8, 6 }, 0);
	bool linked = false;
	for (const Zones::Edge& e : z.nodes(0)[size_t(p0)].edges)
		linked |= e.to == right;
	EXPECT_TRUE(linked);
}

TEST(Zones, TheSearchMarksACorridorAlongTheRoute)
{
	Zones z = make(-1);
	ASSERT_TRUE(z.search({ 4, 8 }, { 16, 8 }, kNormal));
	EXPECT_TRUE(z.in_corridor({ 4, 8 }));
	EXPECT_TRUE(z.in_corridor({ 16, 8 }));
	ASSERT_GE(z.route(0).size(), 2u);
	EXPECT_EQ(z.route(0).front(), z.part({ 4, 8 }, 0));
	EXPECT_EQ(z.route(0).back(), z.part({ 16, 8 }, 0));
	// Every part on the route is inside the corridor; far corners aren't.
	int marked = 0;
	for (int x = 4; x <= 16; ++x)
		for (int y = 4; y <= 16; ++y)
			marked += z.in_corridor({ x, y }) ? 1 : 0;
	EXPECT_LT(marked, 13 * 13);
	EXPECT_FALSE(z.in_corridor({ 4, 16 }));

	// No route for a tank across water; an amphibious unit has one.
	Zones w = make(10);
	EXPECT_FALSE(w.search({ 5, 8 }, { 15, 8 }, kNormal));
	EXPECT_TRUE(w.search({ 5, 8 }, { 15, 8 }, kAmphibious));
}

TEST(Zones, BlockingAroundAStuckCellChangesTheRoute)
{
	Zones z = make(-1);
	ASSERT_TRUE(z.search({ 4, 8 }, { 16, 8 }, kNormal));
	const std::vector<int> first = z.route(0);
	// Pretend a wall stands in column 9 (except at the top): the search got stuck at (8, 8).
	const auto enterable = [](Cell c, int) { return !(c.x == 9 && c.y > 4); };
	z.block_around({ 8, 8 }, kNormal, enterable);
	ASSERT_TRUE(z.search({ 4, 8 }, { 16, 8 }, kNormal));
	EXPECT_NE(z.route(0), first);
	z.clear_blocked();
	ASSERT_TRUE(z.search({ 4, 8 }, { 16, 8 }, kNormal));
	EXPECT_EQ(z.route(0), first);
}
