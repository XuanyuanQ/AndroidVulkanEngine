#pragma once

#include <cstdint>

namespace ave::platform {

enum class AppEventType {
    SurfaceCreated,
    SurfaceDestroyed,
    Pause,
    Resume,
    Touch,
};

struct AppEvent {
    AppEventType type = AppEventType::Resume;
    int32_t x = 0;
    int32_t y = 0;
};

} // namespace ave::platform
