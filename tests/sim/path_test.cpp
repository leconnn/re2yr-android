#include "sim/path/astar.h"

#include <gtest/gtest.h>

#include <set>
#include <utility>

using namespace ra::sim::path;

namespace {

// A flat test grid: walls can't be entered.
struct Grid : SearchMap {
	int w = 32, h = 32;
	std::set<std::pair<int, int>> walls;
	bool exists(Cell c) const override { return c.x >= 0 && c.y >= 0 && c.x < w && c.y < h; }
	int level(Cell) const override { return 0; }
	bool bridge(Cell) const override { return false; }
	Move can_enter(Cell to, int, int, Cell) const override { return walls.count({ to.x, to.y }) ? Move::No : Move::Ok; }
};

Cell walk(Cell start, const Result& r)
{
	Cell c = start;
	for (int d : r.directions)
		c = step(c, d);
	return c;
}

Query query(Cell from, Cell to)
{
	Query q;
	q.start = from;
	q.goal = to;
	return q;
}

} // namespace

TEST(Path, StraightAndDiagonalLines)
{
	Grid g;
	Result r = find_path(g, query({ 2, 5 }, { 7, 5 }));
	ASSERT_TRUE(r.found);
	EXPECT_EQ(r.directions, (std::vector<int> { 2, 2, 2, 2, 2 }));
	EXPECT_NEAR(r.cost, 5 * 1.002f, 1e-4f);

	r = find_path(g, query({ 2, 2 }, { 7, 7 }));
	ASSERT_TRUE(r.found);
	EXPECT_EQ(r.directions, (std::vector<int> { 3, 3, 3, 3, 3 })); // a diagonal step costs the same as a straight one
	EXPECT_EQ(walk({ 2, 2 }, r), (Cell { 7, 7 }));
}

TEST(Path, GoesAroundAWall)
{
	Grid g;
	for (int y = 0; y < 10; ++y)
		g.walls.insert({ 5, y });
	const Result r = find_path(g, query({ 2, 4 }, { 8, 4 }));
	ASSERT_TRUE(r.found);
	Cell c { 2, 4 };
	for (int d : r.directions) {
		c = step(c, d);
		EXPECT_FALSE(g.walls.count({ c.x, c.y })) << c.x << "," << c.y;
	}
	EXPECT_EQ(c, (Cell { 8, 4 }));
	EXPECT_GE(r.directions.size(), 7u);
}

TEST(Path, ABlockedGoalEndsNextToIt)
{
	Grid g;
	g.walls.insert({ 9, 5 });
	const Result r = find_path(g, query({ 2, 5 }, { 9, 5 }));
	ASSERT_TRUE(r.found);
	const Cell end = walk({ 2, 5 }, r);
	EXPECT_EQ(end, (Cell { 8, 5 }));
}

TEST(Path, UnreachableAndTrivialSearches)
{
	Grid g;
	// Box the goal in, two cells thick so the blocked-goal rule doesn't apply to its walls.
	for (int y = 8; y <= 16; ++y)
		for (int x = 8; x <= 16; ++x)
			if (!(x >= 11 && x <= 13 && y >= 11 && y <= 13))
				g.walls.insert({ x, y });
	Result r = find_path(g, query({ 1, 1 }, { 12, 12 }));
	EXPECT_FALSE(r.found);
	EXPECT_GT(r.expanded, 100);

	r = find_path(g, query({ 3, 3 }, { 3, 3 }));
	EXPECT_FALSE(r.found); // already there

	Query limited = query({ 1, 1 }, { 30, 30 });
	limited.max_steps = 5;
	r = find_path(g, limited);
	EXPECT_FALSE(r.found);
}

TEST(Path, EqualCostRoutesFollowTheTieBreakOrder)
{
	// From (5,5) to (5,7) around a single blocking cell at (5,6): going east first (tie-break 0.002 for E,
	// 0.006 for SE) and going west (0.004 / 0.007) cost the same number of steps; the directions' tie-breaks
	// decide, and the search must give the same answer every time.
	Grid g;
	g.walls.insert({ 5, 6 });
	const Result a = find_path(g, query({ 5, 5 }, { 5, 7 }));
	const Result b = find_path(g, query({ 5, 5 }, { 5, 7 }));
	ASSERT_TRUE(a.found);
	EXPECT_EQ(a.directions, b.directions);
	EXPECT_EQ(a.directions.size(), 2u);
	EXPECT_EQ(walk({ 5, 5 }, a), (Cell { 5, 7 }));
	// SE then SW (0.006 + 0.007) beats SW then SE (0.007 + 0.006)? They tie in cost, so the heap order
	// decides: the SE neighbour is created first and pushed before the SW one is held.
	EXPECT_EQ(a.directions, (std::vector<int> { 3, 5 }));
}
