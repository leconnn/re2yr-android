#pragma once

// Floating-point arithmetic in the original game's mode.
//
// The original sets the x87 control word to 0x0E7F at start-up and keeps it
// there: round toward zero, 53-bit (double) precision, exceptions masked.
// Every add, subtract, multiply, divide and square root therefore truncates
// its result, where C++ on x86_64 and arm64 rounds to nearest. Simulation code
// must use these functions (or the Real type below) for any value that can
// affect game state, or results drift apart from the original.
//
// The functions compute the round-to-nearest result and then correct it using
// the exact rounding error (TwoSum or a fused multiply-add). They do not depend
// on the floating-point environment or compiler flags, so they give identical
// bits on every platform.
//
// Not modelled: results in the subnormal range, where the original keeps an
// extended exponent in registers. Game values never get that small.

#include <cstdint>

namespace ra::fpu {

double add(double a, double b);
double sub(double a, double b);
double mul(double a, double b);
double div(double a, double b);
double sqrt(double a);

// Storing a register to a 32-bit float variable (also truncates).
float to_float(double a);

// The original's float-to-integer conversion: truncate toward zero to a
// 64-bit integer. NaN and out-of-range values give INT64_MIN, as the x87 does.
int64_t ftol64(double a);

// Most callers keep only the low 32 bits of the result.
inline int32_t ftol(double a)
{
	return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(ftol64(a))));
}

} // namespace ra::fpu

namespace ra {

// A double whose arithmetic operators follow the original's x87 mode.
// Comparisons and conversions from integers are exact and need no correction.
class Real {
public:
	constexpr Real() = default;
	constexpr explicit Real(double v) : value_(v) {}
	constexpr Real(int v) : value_(v) {}

	constexpr double value() const { return value_; }

	// Truncating conversion to int (the original's ftol).
	int32_t to_int() const { return fpu::ftol(value_); }
	float to_float() const { return fpu::to_float(value_); }

	friend Real operator+(Real a, Real b) { return Real(fpu::add(a.value_, b.value_)); }
	friend Real operator-(Real a, Real b) { return Real(fpu::sub(a.value_, b.value_)); }
	friend Real operator*(Real a, Real b) { return Real(fpu::mul(a.value_, b.value_)); }
	friend Real operator/(Real a, Real b) { return Real(fpu::div(a.value_, b.value_)); }
	constexpr Real operator-() const { return Real(-value_); }

	Real& operator+=(Real b) { return *this = *this + b; }
	Real& operator-=(Real b) { return *this = *this - b; }
	Real& operator*=(Real b) { return *this = *this * b; }
	Real& operator/=(Real b) { return *this = *this / b; }

	friend constexpr bool operator==(Real a, Real b) { return a.value_ == b.value_; }
	friend constexpr bool operator!=(Real a, Real b) { return a.value_ != b.value_; }
	friend constexpr bool operator<(Real a, Real b) { return a.value_ < b.value_; }
	friend constexpr bool operator<=(Real a, Real b) { return a.value_ <= b.value_; }
	friend constexpr bool operator>(Real a, Real b) { return a.value_ > b.value_; }
	friend constexpr bool operator>=(Real a, Real b) { return a.value_ >= b.value_; }

private:
	double value_ = 0.0;
};

inline Real sqrt(Real a)
{
	return Real(fpu::sqrt(a.value()));
}

} // namespace ra
