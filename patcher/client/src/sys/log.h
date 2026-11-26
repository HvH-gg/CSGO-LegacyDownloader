#pragma once

#include <string_view>

namespace csgopatch {

enum class LogLevel { Info, Warn, Error };

/// Minimal logger: appends to `csgo-patcher.log` next to the host executable and
/// mirrors every line to the debugger (DbgView / Visual Studio output). It is
/// deliberately dependency-free and process-wide; the patcher has no need for more.
class Log {
public:
    /// Formats like printf. Keep messages single-line.
    static void write(LogLevel level, const char* fmt, ...);

    static void info(const char* fmt, ...);
    static void warn(const char* fmt, ...);
    static void error(const char* fmt, ...);
};

}  // namespace csgopatch
