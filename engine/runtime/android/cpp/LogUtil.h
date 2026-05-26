// LogUtil.h - Unified C++ logging for Android
#pragma once

#include <android/log.h>
#include <cstdarg>
#include <cstdio>

namespace LogUtil {
/**
 * Log a formatted message with file, line and function information.
 *
 * @param priority Android log priority (ANDROID_LOG_INFO, ANDROID_LOG_ERROR, etc.)
 * @param file     __FILE__ macro
 * @param line     __LINE__ macro
 * @param func     __FUNCTION__ macro
 * @param fmt      printf‑style format string
 */
inline void log(int priority,
                const char* file,
                int line,
                const char* func,
                const char* fmt,
                ...) {
    const char* tag = "AveRuntime"; // Tag used for all native logs
    char buffer[1024];
    // Prefix with location info: [File.cpp:123] Namespace::Class::method -
    int prefix = snprintf(buffer, sizeof(buffer), "[%s:%d] %s - ", file, line, func);
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer + prefix, sizeof(buffer) - prefix, fmt, args);
    va_end(args);
    __android_log_print(priority, tag, "%s", buffer);
}
} // namespace LogUtil

// Convenience macros – behave like Android's LOGI/LOGE etc.
#define LOGI(fmt, ...) LogUtil::log(ANDROID_LOG_INFO,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LogUtil::log(ANDROID_LOG_WARN,    __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LogUtil::log(ANDROID_LOG_DEBUG,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LogUtil::log(ANDROID_LOG_ERROR,   __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
