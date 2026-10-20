#include "core/fastmath.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>

namespace fm = ra::fastmath;

namespace {

double d(const std::string& hex)
{
	const uint64_t u = std::stoull(hex, nullptr, 16);
	double v;
	std::memcpy(&v, &u, 8);
	return v;
}

uint64_t bits(double v)
{
	uint64_t u;
	std::memcpy(&u, &v, 8);
	return u;
}

uint64_t fnv1a(const fm::Table& t)
{
	uint64_t h = 0xCBF29CE484222325ull;
	for (std::size_t i = 0; i < t.size; ++i) {
		for (int b = 0; b < 4; ++b) {
			h ^= (t.data[i] >> (8 * b)) & 0xFFu;
			h *= 0x100000001B3ull;
		}
	}
	return h;
}

} // namespace

// Checksums of the tables stored in the original executable. The engine
// generates its tables from formulas (tools/gen_fastmath_tables.py); these
// prove every entry matches.
TEST(FastMath, TablesMatchOriginal)
{
	struct Expected {
		fm::TableId id;
		std::size_t size;
		uint64_t fnv;
	};
	const Expected expected[] = {
		{ fm::TableId::Sin, 0x2801, 0x74ACB749B33D5AA7ull },
		{ fm::TableId::Tan, 4097, 0x578AF9FEDAF19847ull },
		{ fm::TableId::Asin, 4097, 0x9251751BF3283BC1ull },
		{ fm::TableId::Atan, 4097, 0x4056C36F7F1EAB9Cull },
		{ fm::TableId::Sqrt, 16384, 0x52E69A01B9CF4F49ull },
	};
	for (const auto& e : expected) {
		const fm::Table t = fm::table(e.id);
		EXPECT_EQ(t.size, e.size) << int(e.id);
		EXPECT_EQ(fnv1a(t), e.fnv) << "table " << int(e.id);
	}
}

// fastmath_golden.txt holds results of the original functions executed on
// x87 hardware in the game's floating-point mode.
TEST(FastMath, MatchesOriginalFunctions)
{
	const auto rows = ra::test::read_data_file("fastmath_golden.txt");
	ASSERT_GT(rows.size(), 20000u) << "missing tests/data/fastmath_golden.txt";

	const std::map<std::string, double (*)(double)> unary = {
		{ "sqrt", &fm::sqrt }, { "sin", &fm::sin }, { "cos", &fm::cos }, { "tan", &fm::tan },
		{ "asin", &fm::asin }, { "acos", &fm::acos }, { "atan", &fm::atan },
	};
	std::map<std::string, int> mismatches;
	for (const auto& r : rows) {
		uint64_t expected, actual;
		if (r[0] == "atan2") {
			expected = std::stoull(r[3], nullptr, 16);
			actual = bits(fm::atan2(d(r[1]), d(r[2])));
		} else {
			auto it = unary.find(r[0]);
			ASSERT_NE(it, unary.end()) << "unknown function " << r[0];
			expected = std::stoull(r[2], nullptr, 16);
			actual = bits(it->second(d(r[1])));
		}
		if (expected != actual && ++mismatches[r[0]] <= 3)
			ADD_FAILURE() << r[0] << "(" << r[1] << (r.size() > 3 ? ", " + r[2] : "") << "): expected " << std::hex << expected << " got " << actual;
	}
	for (const auto& [name, count] : mismatches)
		EXPECT_EQ(count, 0) << name;
}

TEST(FastMath, KnownValues)
{
	EXPECT_EQ(fm::sqrt(4.0), 2.0);
	EXPECT_EQ(fm::sqrt(-4.0), 2.0);   // the original takes the absolute value
	EXPECT_EQ(fm::sqrt(0.0), 0.0);
	EXPECT_EQ(fm::sin(0.0), 0.0);
	EXPECT_EQ(fm::cos(0.0), 1.0);
	EXPECT_EQ(fm::atan2(0.0, 0.0), 0.0);
	EXPECT_EQ(fm::atan2(1.0, 0.0), double(1.5707963705062866));
	// Quirk kept from the original: an angle one half-step below zero rounds up
	// to the first positive entry.
	EXPECT_GT(fm::sin(-0.0006), 0.0);
	EXPECT_EQ(fm::sin(-0.0002), 0.0);
}
