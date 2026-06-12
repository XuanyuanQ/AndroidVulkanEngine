#pragma once

#include <cstdarg>
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace LogUtil {

inline void log(int priority,
                char const* file,
                int line,
                char const* func,
                char const* fmt,
                ...)
{
    char buffer[1024];
    int prefix = std::snprintf(buffer, sizeof(buffer), "[%s:%d] %s - ", file, line, func);
    if (prefix < 0) {
        prefix = 0;
    }
    if (static_cast<size_t>(prefix) >= sizeof(buffer)) {
        prefix = static_cast<int>(sizeof(buffer) - 1);
    }

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer + prefix, sizeof(buffer) - static_cast<size_t>(prefix), fmt, args);
    va_end(args);

#if defined(__ANDROID__)
    __android_log_print(priority, "AveRuntime", "%s", buffer);
#else
    FILE* stream = priority >= 6 ? stderr : stdout;
    std::fprintf(stream, "%s\n", buffer);
#endif
}

} // namespace LogUtil

#if defined(__ANDROID__)
#define AVE_LOG_INFO ANDROID_LOG_INFO
#define AVE_LOG_WARN ANDROID_LOG_WARN
#define AVE_LOG_DEBUG ANDROID_LOG_DEBUG
#define AVE_LOG_ERROR ANDROID_LOG_ERROR
#else
#define AVE_LOG_INFO 4
#define AVE_LOG_WARN 5
#define AVE_LOG_DEBUG 3
#define AVE_LOG_ERROR 6
#endif

#define LOGI(fmt, ...) LogUtil::log(AVE_LOG_INFO, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LogUtil::log(AVE_LOG_WARN, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGD(fmt, ...) LogUtil::log(AVE_LOG_DEBUG, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LogUtil::log(AVE_LOG_ERROR, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
