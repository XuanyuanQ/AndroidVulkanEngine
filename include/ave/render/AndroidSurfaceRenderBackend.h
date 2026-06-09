#pragma once

#include "VkCommandBuffer.hpp"
#include "VkFramebufferSet.hpp"
#include "VkFrameSync.hpp"
#include "VkRenderPass.hpp"
#include "VkSwapchain.hpp"
#include "VkTexture.hpp"
#include "ave/render/RenderBackend.h"

#include <cstdint>
#include <vector>

namespace ave::render {

struct AndroidSurfaceRenderResources {
    vkfw::VkCommandBuffer framegraph_command_buffers{};
    vkfw::VkRenderPass framegraph_render_pass{};
    vkfw::VkFramebufferSet framegraph_framebuffers{};
    vkfw::VkRenderPass framegraph_load_render_pass{};
    vkfw::VkFramebufferSet framegraph_load_framebuffers{};
    std::vector<vkfw::VkTexture> depth_textures{};
    std::vector<uint8_t> depth_texture_ready{};
    std::vector<vk::Fence> image_in_flight_fences{};
};

class AndroidSurfaceRenderBackend final : public RenderBackend {
public:
    AndroidSurfaceRenderBackend(AndroidSurfaceRenderResources& resources,
                                core::FrameData const& frame,
                                vkfw::VkContext& ctx,
                                vkfw::VkSwapchain& swapchain,
                                vkfw::VkFrameSync& sync,
                                uint32_t& frame_index);

    FrameGraphRenderResult BeginFrame(RenderFrameRequest& out_request) override;
    FrameGraphRenderResult EndFrame(FrameGraphRenderResult render_result) override;

private:
    static vk::ImageSubresourceRange ColorRange();

    AndroidSurfaceRenderResources& resources_;
    core::FrameData const& frame_;
    vkfw::VkContext& ctx_;
    vkfw::VkSwapchain& swapchain_;
    vkfw::VkFrameSync& sync_;
    uint32_t& frame_index_;
    uint32_t image_index_ = 0;
    vk::Result acquire_result_ = vk::Result::eSuccess;
    vk::CommandBuffer cmd_ = {};
    vk::Image swap_img_ = {};
    bool frame_started_ = false;
};

} // namespace ave::render
