#include "core/fastmath.h"

#include "core/assert.h"
#include "core/fpu.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace ra::fastmath {

namespace {

#include "core/fastmath_tables.inc"

// Constants as stored in the original (floats unless noted).
constexpr double kSinScale = 2607.594482421875;          // 16384 / 2pi, float
constexpr double kTanScale = 651.8986469044033;          // 4096 / 2pi, double
constexpr double kAtanStep = 0.024413354694843292;       // float
constexpr double kHalfPiFloat = 1.5707963705062866;      // float
constexpr double kHalfPiDouble = 1.5707963267948966;     // double
constexpr double kPiDouble = 3.141592653589793;          // double

double entry(const uint32_t* table, int32_t index)
{
	float f;
	std::memcpy(&f, &table[index], sizeof f);
	return f;
}

double from_float_bits(uint32_t bits)
{
	float f;
	std::memcpy(&f, &bits, sizeof f);
	return f;
}

// The original's compares against zero read the x87 flags, which report an
// unordered compare (NaN) as both "equal" and "less".
bool x87_equal_zero(double v)
{
	return v == 0.0 || std::isnan(v);
}

bool x87_less_zero(double v)
{
	return v < 0.0 || std::isnan(v);
}

// n mod m with a non-negative result, computed the way the original does
// (C remainder, then add m if negative). m is a power of two.
int32_t wrap(int32_t n, int32_t m)
{
	int32_t r = n % m;
	if (r < 0)
		r += m;
	return r;
}

// Shared by sin and cos: the table has 8192 steps per turn and the scaled
// angle has one extra bit, used to round up to the next entry.
int32_t sin_index(double radians, int32_t offset, int32_t last)
{
	const int32_t n = fpu::ftol(fpu::mul(radians, kSinScale));
	const bool odd = (n & 1) != 0;
	int32_t index = wrap(n / 2, 0x2000) + offset;
	if (odd && index < last)
		++index;
	return index;
}

// The atan table lookup shared by atan and atan2: |q| in steps of kAtanStep,
// saturating at pi/2. Only the low 32 bits of the conversion are kept.
double atan_lookup(double q)
{
	int32_t n = fpu::ftol(fpu::div(q, kAtanStep));
	n = static_cast<int32_t>(n < 0 ? 0u - static_cast<uint32_t>(n) : static_cast<uint32_t>(n));
	if (n >= 0x1001)
		return kHalfPiFloat;
	RA_ASSERT_MSG(n >= 0, "fastmath atan index out of range");
	return entry(kAtanTable, n);
}

} // namespace

double sqrt(double x)
{
	if (x87_equal_zero(x))
		return 0.0;
	if (x < 0.0)
		x = fpu::mul(x, -1.0);

	const float f = fpu::to_float(x);
	uint32_t bits;
	std::memcpy(&bits, &f, sizeof bits);

	const int32_t exponent = static_cast<int32_t>(bits >> 23) - 127;
	uint32_t mantissa = bits & 0x7FFFFFu;
	if (exponent & 1)
		mantissa |= 0x800000u;
	const int32_t half_exponent = static_cast<int16_t>(exponent) >> 1;
	const uint32_t result = kSqrtTable[mantissa >> 10] + (static_cast<uint32_t>(half_exponent + 127) << 23);
	return from_float_bits(result);
}

double sin(double radians)
{
	return entry(kSinTable, sin_index(radians, 0, 0x1FFF));
}

double cos(double radians)
{
	return entry(kSinTable, sin_index(radians, 0x800, 0x27FF));
}

double tan(double radians)
{
	const int32_t n = fpu::ftol(fpu::mul(radians, kTanScale));
	return entry(kTanTable, wrap(n, 0x1000));
}

double asin(double x)
{
	const int32_t n = fpu::ftol(fpu::mul(fpu::add(x, 1.0), -2048.0));
	const int32_t index = -n;
	// The original reads outside the table for |x| > 1; game code never does that.
	RA_ASSERT_MSG(index >= 0 && index <= 4096, "fastmath::asin argument outside [-1, 1]");
	return entry(kAsinTable, index);
}

double acos(double x)
{
	return fpu::sub(kHalfPiDouble, asin(x));
}

double atan(double x)
{
	const double sign_test = fpu::to_float(x);
	const double r = atan_lookup(x);
	return x87_less_zero(sign_test) ? -r : r;
}

double atan2(double y, double x)
{
	const double yf = fpu::to_float(y);
	const double xf = fpu::to_float(x);
	if (x87_equal_zero(xf)) {
		if (x87_equal_zero(yf))
			return 0.0;
		return yf > 0.0 ? kHalfPiFloat : -kHalfPiFloat;
	}
	double r = atan_lookup(fpu::div(yf, xf));
	if (x87_less_zero(xf))
		r = fpu::sub(kPiDouble, r);
	if (x87_less_zero(yf))
		r = -r;
	return r;
}

Table table(TableId id)
{
	switch (id) {
	case TableId::Sin: return { kSinTable, std::size(kSinTable) };
	case TableId::Tan: return { kTanTable, std::size(kTanTable) };
	case TableId::Asin: return { kAsinTable, std::size(kAsinTable) };
	case TableId::Atan: return { kAtanTable, std::size(kAtanTable) };
	case TableId::Sqrt: return { kSqrtTable, std::size(kSqrtTable) };
	}
	return { nullptr, 0 };
}

} // namespace ra::fastmath
