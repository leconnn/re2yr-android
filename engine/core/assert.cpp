#include "core/assert.h"

#include "core/log.h"

#include <cstdlib>

namespace ra {

void assert_failed(const char* expression, const char* message, const char* file, int line)
{
	if (message)
		log_message(LogLevel::Error, "Assertion failed: %s (%s) at %s:%d", expression, message, file, line);
	else
		log_message(LogLevel::Error, "Assertion failed: %s at %s:%d", expression, file, line);
	std::abort();
}

} // namespace ra
