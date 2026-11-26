#include "sys/log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace csgopatch {
namespace {

const char* level_tag(LogLevel level) {
    switch (level) {
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

/// Resolves the log path once: <host-exe-dir>\csgo-patcher.log.
const wchar_t* log_path() {
    static wchar_t path[MAX_PATH] = {};
    static bool resolved = false;
    if (!resolved) {
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            if (wchar_t* slash = wcsrchr(path, L'\\')) {
                wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"csgo-patcher.log");
            }
        } else {
            wcscpy_s(path, L"csgo-patcher.log");
        }
        resolved = true;
    }
    return path;
}

void emit(LogLevel level, const char* fmt, va_list args) {
    char body[512];
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);

    char line[600];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[csgo-patcher] %s: %s", level_tag(level), body);

    OutputDebugStringA(line);
    OutputDebugStringA("\n");

    static std::mutex file_mutex;
    std::lock_guard<std::mutex> lock(file_mutex);
    if (FILE* f = _wfopen(log_path(), L"a")) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

}  // namespace

void Log::write(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit(level, fmt, args);
    va_end(args);
}

void Log::info(const char* fmt, ...) {
    va_list args; va_start(args, fmt); emit(LogLevel::Info, fmt, args); va_end(args);
}
void Log::warn(const char* fmt, ...) {
    va_list args; va_start(args, fmt); emit(LogLevel::Warn, fmt, args); va_end(args);
}
void Log::error(const char* fmt, ...) {
    va_list args; va_start(args, fmt); emit(LogLevel::Error, fmt, args); va_end(args);
}

}  // namespace csgopatch
