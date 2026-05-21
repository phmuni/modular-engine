// Minimal logging API for the engine
#pragma once

#include <cstdarg>

namespace Logger {
enum class Level { Debug = 0, Info, Warn, Error, Fatal };

void setLevel(Level lvl);
Level getLevel();

// Logging printf-like.
void log(Level lvl, const char *file, int line, const char *fmt, ...);
void vlog(Level lvl, const char *file, int line, const char *fmt, va_list args);

} // namespace Logger

// Macros
#define LOG_D(fmt, ...) Logger::log(Logger::Level::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) Logger::log(Logger::Level::Info, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) Logger::log(Logger::Level::Warn, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) Logger::log(Logger::Level::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_F(fmt, ...) Logger::log(Logger::Level::Fatal, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
