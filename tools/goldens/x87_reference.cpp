// Generates reference results for the original game's floating-point mode.
//
// The original runs with the x87 control word 0x0E7F: round toward zero,
// 53-bit precision, all exceptions masked. This program executes each
// operation on real x87 hardware in that mode and prints the results, which
// tests/core/fpu_test.cpp compares against the portable implementation.
//
// Build (32-bit MSVC only, it uses inline x87 assembly):
//   cl /nologo /O2 /EHsc x87_reference.cpp
//   x87_reference.exe > tests/data/x87_golden.txt

#include <cstdint>
#include <cstdio>
#include <cstring>

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error "x87_reference must be built with 32-bit MSVC"
#endif

static uint64_t g_state = 0x0123456789ABCDEFull;

static uint64_t splitmix64()
{
	uint64_t z = (g_state += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

static uint64_t bits(double d)
{
	uint64_t u;
	std::memcpy(&u, &d, 8);
	return u;
}

static uint32_t fbits(float f)
{
	uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

static double from_bits(uint64_t u)
{
	double d;
	std::memcpy(&d, &u, 8);
	return d;
}

// A random double in one of several shapes the game's maths produces.
static double sample()
{
	uint64_t r = splitmix64();
	switch (r & 7) {
	case 0: // integers
		return double(int32_t(splitmix64() >> 40) - (1 << 23));
	case 1: // small percentages like 0.01 * k
		return double(int32_t(splitmix64() % 2001) - 1000) * 0.01;
	case 2: // values in [0, 1)
		return double(splitmix64() >> 11) * (1.0 / 9007199254740992.0);
	case 3: // lepton-scale values
		return double(int64_t(splitmix64() >> 34) - (1ll << 29)) / 256.0;
	default: { // random mantissa, exponent in [-40, 40]
		uint64_t mant = splitmix64() & 0x000FFFFFFFFFFFFFull;
		uint64_t exp = uint64_t(1023 - 40 + (splitmix64() % 81));
		uint64_t sign = (r >> 8) & 1;
		return from_bits((sign << 63) | (exp << 52) | mant);
	}
	}
}

static unsigned short g_cw = 0x0E7F;

static double x87_add(double a, double b) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fadd b } __asm { fstp r } return r; }
static double x87_sub(double a, double b) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fsub b } __asm { fstp r } return r; }
static double x87_mul(double a, double b) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fmul b } __asm { fstp r } return r; }
static double x87_div(double a, double b) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fdiv b } __asm { fstp r } return r; }
static double x87_sqrt(double a) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fsqrt } __asm { fstp r } return r; }
static float x87_to_float(double a) { float r; __asm { fldcw g_cw } __asm { fld a } __asm { fstp r } return r; }
static int64_t x87_ftol(double a) { int64_t r; __asm { fldcw g_cw } __asm { fld a } __asm { fistp r } return r; }

// a * b + c kept in a register between the two operations, as compiled code does.
static double x87_muladd(double a, double b, double c) { double r; __asm { fldcw g_cw } __asm { fld a } __asm { fmul b } __asm { fadd c } __asm { fstp r } return r; }

int main()
{
	std::printf("# x87 reference results, control word 0x0E7F (round toward zero, 53-bit precision).\n");
	std::printf("# <op> <operand bits...> <result bits>, all hexadecimal IEEE-754.\n");
	const int count = 3000;
	for (int i = 0; i < count; ++i) {
		double a = sample(), b = sample(), c = sample();
		if (b == 0.0)
			b = 1.0;
		std::printf("add %016llx %016llx %016llx\n", bits(a), bits(b), bits(x87_add(a, b)));
		std::printf("sub %016llx %016llx %016llx\n", bits(a), bits(b), bits(x87_sub(a, b)));
		std::printf("mul %016llx %016llx %016llx\n", bits(a), bits(b), bits(x87_mul(a, b)));
		std::printf("div %016llx %016llx %016llx\n", bits(a), bits(b), bits(x87_div(a, b)));
		double s = a < 0 ? -a : a;
		std::printf("sqrt %016llx %016llx\n", bits(s), bits(x87_sqrt(s)));
		std::printf("tofloat %016llx %08x\n", bits(a), fbits(x87_to_float(a)));
		std::printf("muladd %016llx %016llx %016llx %016llx\n", bits(a), bits(b), bits(c), bits(x87_muladd(a, b, c)));
		double big = a * 1.0e6;
		std::printf("ftol %016llx %016llx\n", bits(big), (unsigned long long)x87_ftol(big));
	}
	// ftol edge cases
	const double edges[] = { 0.0, -0.0, 0.5, -0.5, 0.999999999, -0.999999999, 2147483647.9, -2147483648.9,
		2147483648.0, 4294967296.5, -4294967297.5, 9.2233720368547758e18, -9.2233720368547758e18, 1.0e19, -1.0e19 };
	for (double e : edges)
		std::printf("ftol %016llx %016llx\n", bits(e), (unsigned long long)x87_ftol(e));
	return 0;
}
