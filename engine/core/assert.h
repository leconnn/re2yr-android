#pragma once

// Assertions that stay on in release builds. A failed assertion means the
// simulation can no longer be trusted to match the original, so it stops.

namespace ra {

[[noreturn]] void assert_failed(const char* expression, const char* message, const char* file, int line);

} // namespace ra

#define RA_ASSERT(cond) \
	do { \
		if (!(cond)) \
			::ra::assert_failed(#cond, nullptr, __FILE__, __LINE__); \
	} while (false)

#define RA_ASSERT_MSG(cond, msg) \
	do { \
		if (!(cond)) \
			::ra::assert_failed(#cond, (msg), __FILE__, __LINE__); \
	} while (false)
