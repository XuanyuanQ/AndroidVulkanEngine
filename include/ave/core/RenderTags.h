#pragma once

#include <cstdint>

namespace ave::core {

// Layer classification (bit mask).
enum class RenderLayer : uint32_t {
    World = 1u << 0,
    UI = 1u << 1,
    FX = 1u << 2,
};

// Pass participation bits (see docs/frame_data_contract_zh.md).
enum class RenderPassBit : uint32_t {
    None = 0,
    Shadow = 1u << 0,
    DepthPrepass = 1u << 1,
    ForwardOpaque = 1u << 2,
    ForwardTransparent = 1u << 3,
    UI = 1u << 4,
    ToneMapping = 1u << 5,
    Compute = 1u << 6,
    VolumetricLight = 1u << 7,
};

constexpr uint32_t ToMask(RenderLayer layer)
{
    return static_cast<uint32_t>(layer);
}

constexpr uint32_t ToMask(RenderPassBit pass)
{
    return static_cast<uint32_t>(pass);
}

constexpr bool HasPassBit(uint32_t pass_mask, RenderPassBit bit)
{
    return (pass_mask & ToMask(bit)) != 0u;
}

constexpr bool HasLayer(uint32_t layer_mask, RenderLayer layer)
{
    return (layer_mask & ToMask(layer)) != 0u;
}

// Default routing for opaque world meshes.
inline uint32_t DefaultWorldPassMask()
{
    return ToMask(RenderPassBit::DepthPrepass) | ToMask(RenderPassBit::ForwardOpaque) |
           ToMask(RenderPassBit::Shadow);
}

inline uint32_t DefaultUiPassMask()
{
    return ToMask(RenderPassBit::UI);
}

// Common render queue values (Unity-like).
constexpr uint32_t kQueueOpaque = 2000;
constexpr uint32_t kQueueAlphaTest = 2450;
constexpr uint32_t kQueueTransparent = 3000;
constexpr uint32_t kQueueOverlay = 4000;

} // namespace ave::core
