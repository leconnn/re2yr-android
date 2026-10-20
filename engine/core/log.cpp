#include "core/log.h"

#include <atomic>
#include <cstdio>

namespace ra {

namespace {

void stderr_sink(LogLevel level, const char* message)
{
	static constexpr const char* names[] = { "debug", "info", "warning", "error" };
	std::fprintf(stderr, "[%s] %s\n", names[static_cast<int>(level)], message);
}

std::atomic<LogSink> g_sink { &stderr_sink };

} // namespace

void set_log_sink(LogSink sink)
{
	g_sink.store(sink ? sink : &stderr_sink);
}

void log_message_v(LogLevel level, const char* format, va_list args)
{
	char buffer[1024];
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	g_sink.load()(level, buffer);
}

void log_message(LogLevel level, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	log_message_v(level, format, args);
	va_end(args);
}

} // namespace ra
