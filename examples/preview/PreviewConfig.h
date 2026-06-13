#pragma once

#include <chrono>
#include <cstdint>

namespace ave_preview {

inline constexpr uint32_t kFramesInFlight = 2;
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr auto kHotReloadInterval = std::chrono::milliseconds(500);
inline constexpr auto kHotReloadDebounce = std::chrono::milliseconds(300);

} // namespace ave_preview
