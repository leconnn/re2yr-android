#pragma once

// The original game's maths functions.
//
// The game doesn't use the C library's sqrt, sin, cos and so on. It uses its
// own table lookups, which are only accurate to about 13 bits and have some
// quirks (for example sin of a tiny negative angle is a small positive value).
// Simulation code must call these so that results such as distances, sight
// ranges and facings match the original exactly.
//
// Every function returns the value of a 32-bit float table entry (or a value
// computed from one), widened to double, as the original returns it on the
// x87 stack. Arithmetic inside follows the original's x87 mode (core/fpu.h).

#include <cstddef>
#include <cstdint>

namespace ra::fastmath {

double sqrt(double x);
double sin(double radians);
double cos(double radians);
double tan(double radians);
double asin(double x);     // x must be in [-1, 1]
double acos(double x);     // x must be in [-1, 1]
double atan(double x);
double atan2(double y, double x);

// The raw lookup tables (float bit patterns), for tests.
struct Table {
	const uint32_t* data;
	std::size_t size;
};
enum class TableId { Sin, Tan, Asin, Atan, Sqrt };
Table table(TableId id);

} // namespace ra::fastmath
