#pragma once

// Minimal printf-style logging. The platform layer can redirect output
// (for example to Android's logcat) with set_log_sink.

#include <cstdarg>

namespace ra {

enum class LogLevel { Debug, Info, Warning, Error };

using LogSink = void (*)(LogLevel level, const char* message);

void set_log_sink(LogSink sink);
void log_message(LogLevel level, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
	__attribute__((format(printf, 2, 3)))
#endif
	;
void log_message_v(LogLevel level, const char* format, va_list args);

} // namespace ra

#define RA_LOG_DEBUG(...) ::ra::log_message(::ra::LogLevel::Debug, __VA_ARGS__)
#define RA_LOG_INFO(...) ::ra::log_message(::ra::LogLevel::Info, __VA_ARGS__)
#define RA_LOG_WARN(...) ::ra::log_message(::ra::LogLevel::Warning, __VA_ARGS__)
#define RA_LOG_ERROR(...) ::ra::log_message(::ra::LogLevel::Error, __VA_ARGS__)
