#include "core/fpu.h"

#include <cfloat>
#include <cmath>
#include <limits>

#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD != 0
#error "fpu.cpp needs FLT_EVAL_METHOD == 0 (SSE2 or NEON doubles, not x87)"
#endif

namespace ra::fpu {

namespace {

// Moves a round-to-nearest result one step toward zero when the exact result
// lies between it and zero. err is (exact result - rounded result).
double toward_zero(double rounded, double err)
{
	if (err == 0.0 || rounded == 0.0)
		return rounded;
	if ((err < 0.0) != (rounded < 0.0))
		return std::nextafter(rounded, 0.0);
	return rounded;
}

// Round toward zero never overflows to infinity: it stops at the largest finite value.
bool overflowed(double result, double a, double b)
{
	return std::isinf(result) && std::isfinite(a) && std::isfinite(b);
}

double max_finite(double sign_of)
{
	return std::copysign(DBL_MAX, sign_of);
}

} // namespace

double add(double a, double b)
{
	const double s = a + b;
	if (overflowed(s, a, b))
		return max_finite(s);
	if (!std::isfinite(s))
		return s;
	// TwoSum: exact error of the rounded sum.
	const double bv = s - a;
	const double av = s - bv;
	const double err = (a - av) + (b - bv);
	return toward_zero(s, err);
}

double sub(double a, double b)
{
	return add(a, -b);
}

double mul(double a, double b)
{
	const double p = a * b;
	if (overflowed(p, a, b))
		return max_finite(p);
	if (!std::isfinite(p))
		return p;
	return toward_zero(p, std::fma(a, b, -p));
}

double div(double a, double b)
{
	const double q = a / b;
	if (b != 0.0 && overflowed(q, a, b))
		return max_finite(q);
	if (!std::isfinite(q) || q == 0.0)
		return q;
	// r = a - q*b exactly; the exact quotient differs from q by r / b.
	const double r = std::fma(-q, b, a);
	if (r == 0.0)
		return q;
	const bool exact_minus_q_negative = (r < 0.0) != (b < 0.0);
	if (exact_minus_q_negative != (q < 0.0))
		return std::nextafter(q, 0.0);
	return q;
}

double sqrt(double a)
{
	const double s = std::sqrt(a);
	if (!(a > 0.0) || !std::isfinite(s))
		return s;
	// a - s*s < 0 means s is above the exact root.
	if (std::fma(-s, s, a) < 0.0)
		return std::nextafter(s, 0.0);
	return s;
}

float to_float(double a)
{
	const float f = static_cast<float>(a);
	if (std::isinf(f) && std::isfinite(a))
		return std::copysign(FLT_MAX, f);
	if (!std::isfinite(f) || f == 0.0f)
		return f;
	if (std::fabs(static_cast<double>(f)) > std::fabs(a))
		return std::nextafter(f, 0.0f);
	return f;
}

int64_t ftol64(double a)
{
	constexpr int64_t indefinite = std::numeric_limits<int64_t>::min();
	if (std::isnan(a))
		return indefinite;
	const double t = std::trunc(a);
	// Valid range is [-2^63, 2^63).
	if (t >= 9223372036854775808.0 || t < -9223372036854775808.0)
		return indefinite;
	return static_cast<int64_t>(t);
}

} // namespace ra::fpu
