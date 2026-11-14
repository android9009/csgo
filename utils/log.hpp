#pragma once
namespace Log { enum class Level { Info, Warning, Error }; void Print(Level level, const char* format, ...); }
#define LOG_INFO(...) Log::Print(Log::Level::Info, __VA_ARGS__)
#define LOG_WARN(...) Log::Print(Log::Level::Warning, __VA_ARGS__)
#define LOG_ERROR(...) Log::Print(Log::Level::Error, __VA_ARGS__)
