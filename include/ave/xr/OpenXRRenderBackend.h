#pragma once

#include "ave/render/RenderBackend.h"
#include "VkTexture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace ave::xr {

class OpenXRRuntime;

// OpenXR-facing render backend shell. The real OpenXR integration will fill
// FrameTargets from XR swapchain images after xrAcquireSwapchainImage.
class OpenXRRenderBackend final : public render::RenderBackend {
public:
    struct FrameTargets {
        core::FrameData const* frame = nullptr;
        vkfw::VkContext* vk = nullptr;
        vk::CommandBuffer command_buffer = {};
        std::vector<render::RenderViewTarget> views{};
    };

    void SetNextFrameTargets(FrameTargets targets);
    void ClearNextFrameTargets();

    bool InitializeGraphics(OpenXRRuntime& runtime);
    bool InitializeFrameResources(vkfw::VkContext& ctx);
    void ShutdownFrameResources(vkfw::VkContext& ctx);
    void ShutdownGraphics(OpenXRRuntime& runtime);
    bool HasGraphics() const noexcept { return xr_vulkan_instance_ != nullptr && xr_vulkan_device_ != nullptr; }
    bool HasSwapchain() const noexcept { return xr_swapchain_ != nullptr && !xr_swapchain_image_views_.empty(); }
    void* VulkanInstanceHandle() const noexcept { return xr_vulkan_instance_; }
    void* VulkanPhysicalDeviceHandle() const noexcept { return xr_physical_device_; }
    void* VulkanDeviceHandle() const noexcept { return xr_vulkan_device_; }
    uint32_t GraphicsQueueFamilyIndex() const noexcept { return xr_queue_family_index_; }
    bool SupportsDynamicRendering() const noexcept { return xr_supports_dynamic_rendering_; }
    uint32_t SwapchainWidth() const noexcept { return xr_swapchain_width_; }
    uint32_t SwapchainHeight() const noexcept { return xr_swapchain_height_; }
    bool TryGetXRUiPointerNdc(float& out_x_ndc, float& out_y_ndc) const;
    void UpdateXRUiPointerRay();
    bool IsXRUiPointerActive() const noexcept;
    bool HasXRUiPointerHit() const noexcept { return xr_ui_pointer_has_hit_; }
    glm::vec3 XRUiPointerRayOrigin() const noexcept { return xr_ui_pointer_ray_origin_; }
    glm::vec3 XRUiPointerRayEnd() const noexcept { return xr_ui_pointer_ray_end_; }
    glm::vec3 XRUiPointerHitPosition() const noexcept { return xr_ui_pointer_hit_position_; }

    bool BeginRuntimeFrame(core::FrameData const& frame);
    void EndRuntimeFrame();

    render::FrameGraphRenderResult BeginFrame(render::RenderFrameRequest& out_request) override;
    render::FrameGraphRenderResult EndFrame(render::FrameGraphRenderResult render_result) override;

private:
    OpenXRRuntime* runtime_ = nullptr;
    FrameTargets next_targets_{};
    bool has_next_targets_ = false;
    bool frame_started_ = false;
    bool runtime_frame_started_ = false;
    bool xr_session_begun_ = false;
    bool logged_waiting_for_session_ready_ = false;
    bool xr_frame_begun_ = false;
    bool xr_frame_should_render_ = false;
    int64_t xr_predicted_display_time_ = 0;
    uint32_t xr_acquired_image_index_ = 0;
    glm::vec3 xr_origin_position_{0.0f, 0.0f, 0.0f};
    float xr_origin_yaw_radians_ = 0.0f;
    int64_t xr_last_input_time_ = 0;
    uint64_t xr_locomotion_log_counter_ = 0;
    bool xr_ui_pointer_has_hit_ = false;
    glm::vec3 xr_ui_pointer_ray_origin_{0.0f, 0.0f, 0.0f};
    glm::vec3 xr_ui_pointer_ray_end_{0.0f, 0.0f, -1.0f};
    glm::vec3 xr_ui_pointer_hit_position_{0.0f, 0.0f, -1.0f};
    void* xr_vulkan_instance_ = nullptr;
    void* xr_vulkan_device_ = nullptr;
    void* xr_physical_device_ = nullptr;
    void* xr_queue_ = nullptr;
    void* xr_swapchain_ = nullptr;
    uint32_t xr_swapchain_width_ = 0;
    uint32_t xr_swapchain_height_ = 0;
    int64_t xr_swapchain_format_ = 0;
    uint32_t xr_queue_family_index_ = ~0u;
    bool xr_supports_dynamic_rendering_ = false;
    void* xr_command_pool_ = nullptr;
    void* xr_command_buffer_ = nullptr;
    core::FrameData xr_frame_data_{};
    std::vector<uint64_t> xr_swapchain_images_{};
    std::vector<void*> xr_swapchain_image_views_{};
    std::vector<void*> xr_swapchain_eye_image_views_{};
    std::vector<vkfw::VkTexture> depth_textures_{};
    std::vector<uint8_t> depth_texture_ready_{};
};

} // namespace ave::xr
