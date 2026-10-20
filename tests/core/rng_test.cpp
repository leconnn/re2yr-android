#include "core/random.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <cstdlib>

using ra::Random2;

// rng_golden.txt was produced by executing the original game's own
// Seed / Next / Ranged code, so this checks the sequence bit for bit.
TEST(Random2, MatchesOriginalSequences)
{
	const auto rows = ra::test::read_data_file("rng_golden.txt");
	ASSERT_GT(rows.size(), 5000u) << "missing tests/data/rng_golden.txt";

	Random2 rng;
	int checked = 0;
	for (const auto& r : rows) {
		const std::string& op = r[0];
		if (op == "seed") {
			rng.seed(static_cast<uint32_t>(std::stoul(r[1])));
		} else if (op == "next") {
			ASSERT_EQ(rng.next(), static_cast<uint32_t>(std::stoul(r[1]))) << "after " << checked << " checks";
			++checked;
		} else if (op == "ranged") {
			const int32_t lo = std::stoi(r[1]);
			const int32_t hi = std::stoi(r[2]);
			ASSERT_EQ(rng.ranged(lo, hi), std::stoi(r[3])) << "Ranged(" << lo << ", " << hi << ") after " << checked << " checks";
			++checked;
		} else if (op == "state") {
			EXPECT_EQ(rng.index1(), std::stoi(r[1]));
			EXPECT_EQ(rng.index2(), std::stoi(r[2]));
			EXPECT_EQ(rng.table()[0], static_cast<uint32_t>(std::stoul(r[3])));
			EXPECT_EQ(rng.table()[249], static_cast<uint32_t>(std::stoul(r[4])));
		} else {
			FAIL() << "unknown line type " << op;
		}
	}
	EXPECT_GT(checked, 5000);
}

TEST(Random2, DisabledReturnsZeroAndLo)
{
	Random2 rng(12345);
	rng.set_disabled(true);
	EXPECT_EQ(rng.next(), 0u);
	EXPECT_EQ(rng.ranged(7, 100), 7);
	EXPECT_EQ(rng.index1(), 0);
	rng.set_disabled(false);
	EXPECT_NE(rng.next(), 0u);
}

TEST(Random2, RangeOfTwoToThe31MinusOneSkipsTheDraw)
{
	Random2 rng(1);
	EXPECT_EQ(rng.ranged(0, 0x7FFFFFFF), static_cast<int32_t>(0x80000000u));
	EXPECT_EQ(rng.index1(), 0);
}

TEST(Random2, StateRoundTrip)
{
	Random2 a(99);
	for (int i = 0; i < 300; ++i)
		a.next();
	Random2 b;
	b.set_state(a.index1(), a.index2(), a.table());
	for (int i = 0; i < 1000; ++i)
		ASSERT_EQ(a.next(), b.next());
}
