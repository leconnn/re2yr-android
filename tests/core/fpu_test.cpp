#include "core/fpu.h"

#include "test_data.h"

#include <gtest/gtest.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace fpu = ra::fpu;

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

uint32_t fbits(float v)
{
	uint32_t u;
	std::memcpy(&u, &v, 4);
	return u;
}

} // namespace

// x87_golden.txt holds results computed on real x87 hardware with the
// original's control word 0x0E7F (tools/goldens/x87_reference.cpp).
TEST(Fpu, MatchesX87Hardware)
{
	const auto rows = ra::test::read_data_file("x87_golden.txt");
	ASSERT_GT(rows.size(), 20000u) << "missing tests/data/x87_golden.txt";

	int mismatches = 0;
	for (const auto& r : rows) {
		const std::string& op = r[0];
		uint64_t expected = 0, actual = 0;
		if (op == "add") {
			expected = std::stoull(r[3], nullptr, 16);
			actual = bits(fpu::add(d(r[1]), d(r[2])));
		} else if (op == "sub") {
			expected = std::stoull(r[3], nullptr, 16);
			actual = bits(fpu::sub(d(r[1]), d(r[2])));
		} else if (op == "mul") {
			expected = std::stoull(r[3], nullptr, 16);
			actual = bits(fpu::mul(d(r[1]), d(r[2])));
		} else if (op == "div") {
			expected = std::stoull(r[3], nullptr, 16);
			actual = bits(fpu::div(d(r[1]), d(r[2])));
		} else if (op == "sqrt") {
			expected = std::stoull(r[2], nullptr, 16);
			actual = bits(fpu::sqrt(d(r[1])));
		} else if (op == "tofloat") {
			expected = std::stoull(r[2], nullptr, 16);
			actual = fbits(fpu::to_float(d(r[1])));
		} else if (op == "muladd") {
			expected = std::stoull(r[4], nullptr, 16);
			actual = bits(fpu::add(fpu::mul(d(r[1]), d(r[2])), d(r[3])));
		} else if (op == "ftol") {
			expected = std::stoull(r[2], nullptr, 16);
			actual = static_cast<uint64_t>(fpu::ftol64(d(r[1])));
		} else {
			FAIL() << "unknown line type " << op;
		}
		if (expected != actual) {
			if (++mismatches <= 10)
				ADD_FAILURE() << op << " " << r[1] << (r.size() > 3 ? " " + r[2] : "") << ": expected " << std::hex << expected << " got " << actual;
		}
	}
	EXPECT_EQ(mismatches, 0);
}

TEST(Fpu, TruncatesInsteadOfRounding)
{
	// 1/10 rounds up in its last bit under round-to-nearest; truncation must differ.
	const double nearest = 1.0 / 10.0;
	const double truncated = fpu::div(1.0, 10.0);
	EXPECT_EQ(truncated, std::nextafter(nearest, 0.0));
	EXPECT_EQ(fpu::div(-1.0, 10.0), -truncated);
	// 1/3 rounds down anyway, so both modes agree.
	EXPECT_EQ(fpu::div(1.0, 3.0), 1.0 / 3.0);
	EXPECT_EQ(fpu::div(6.0, 3.0), 2.0);
}

TEST(Fpu, OverflowStopsAtLargestFinite)
{
	EXPECT_EQ(fpu::mul(DBL_MAX, 2.0), DBL_MAX);
	EXPECT_EQ(fpu::add(-DBL_MAX, -DBL_MAX), -DBL_MAX);
	EXPECT_EQ(fpu::to_float(1.0e300), FLT_MAX);
	EXPECT_TRUE(std::isinf(fpu::div(1.0, 0.0)));
}

TEST(Fpu, Ftol)
{
	EXPECT_EQ(fpu::ftol(2.9), 2);
	EXPECT_EQ(fpu::ftol(-2.9), -2);
	EXPECT_EQ(fpu::ftol(4294967296.0 + 5.0), 5); // low 32 bits of the 64-bit result
	EXPECT_EQ(fpu::ftol64(std::numeric_limits<double>::quiet_NaN()), std::numeric_limits<int64_t>::min());
}

TEST(Real, OperatorsUseTruncation)
{
	const ra::Real third = ra::Real(1) / ra::Real(3);
	EXPECT_EQ(third.value(), fpu::div(1.0, 3.0));
	EXPECT_EQ((ra::Real(10) * ra::Real(0.3)).to_int(), 2); // 2.9999... truncates to 2
	EXPECT_EQ(ra::sqrt(ra::Real(2)).value(), fpu::sqrt(2.0));
}
