#pragma once

#include <cstdint>
#include <memory>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace vkfw {

class VkContext;

enum class TextureFormat {
    R8G8B8A8_SRGB,
    R8G8B8A8_UNORM,
    B8G8R8A8_SRGB,
    B8G8R8A8_UNORM,
    R32G32B32A32_SFLOAT,
    D24_UNORM_S8_UINT,
    D32_SFLOAT
};

enum class TextureUsage {
    Sampled = 1u << 0,
    ColorAttachment = 1u << 1,
    DepthStencilAttachment = 1u << 2,
    TransferSrc = 1u << 3,
    TransferDst = 1u << 4
};

struct TextureInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t mip_levels = 1;
    TextureFormat format = TextureFormat::R8G8B8A8_SRGB;
    TextureUsage usage = TextureUsage::Sampled;
    bool mipmap = false;
};

class VkTexture {
public:
    VkTexture() = default;
    ~VkTexture() = default;

    VkTexture(VkTexture&&) noexcept = default;
    VkTexture& operator=(VkTexture&&) noexcept = default;

    VkTexture(VkTexture const&) = delete;
    VkTexture& operator=(VkTexture const&) = delete;

    bool Init(VkContext& ctx, TextureInfo const& info);
    void Shutdown(VkContext& ctx);

    bool IsInitialized() const noexcept { return image_ != nullptr; }
    
    vk::Image Handle() const noexcept { return *image_; }
    vk::ImageView View() const noexcept { return *image_view_; }
    vk::DeviceMemory Memory() const noexcept { return *memory_; }
    vk::Format Format() const noexcept { return format_; }
    vk::Extent3D Extent() const noexcept { return extent_; }
    void UpdateData(VkContext& ctx, void const* data, uint32_t size);

private:
    std::unique_ptr<vk::raii::Image> image_;
    std::unique_ptr<vk::raii::ImageView> image_view_;
    std::unique_ptr<vk::raii::DeviceMemory> memory_;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent3D extent_ = {};
    TextureUsage usage_ = TextureUsage::Sampled;
};

} // namespace vkfw
