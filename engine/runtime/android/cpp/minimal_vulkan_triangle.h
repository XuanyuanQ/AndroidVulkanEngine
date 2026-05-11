#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"

#include "ave/rhi/VulkanDevice.h"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <android/asset_manager.h>
#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace ave::android {

class MinimalVulkanTriangle {
public:
    bool create(AAssetManager* assets, std::string project_path);
    void destroy();
    void setSurface(ANativeWindow* window);
    void clearSurface();
    void resize(int width, int height);

private:
    bool createRenderPass();
    bool loadSceneMesh();
    bool createVertexBuffer();
    bool createPipeline();
    bool createFramebuffers();
    bool createCommandPoolAndBuffers();
    void recordCommandBuffer(vk::raii::CommandBuffer& command_buffer, uint32_t image_index);
    void drawFrame();

    void cleanupSurfaceResources();
    void logProjectAsset() const;
    std::vector<uint32_t> readShaderAsset(char const* path) const;
    std::string readTextAsset(char const* path) const;

    struct Vertex {
        float position[3];
        float color[4];
    };

    AAssetManager* assets_ = nullptr;
    std::string project_path_;
    ANativeWindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    vkfw::VkContext ctx_{};
    vkfw::VkSwapchain swapchainWrap_{};
    vkfw::VkFrameSync sync_{};
    uint32_t frame_index_ = 0;

    // Vulkan objects using raii wrappers

    vk::raii::RenderPass render_pass_ = nullptr;
    vk::raii::PipelineLayout pipeline_layout_ = nullptr;
    vk::raii::Pipeline pipeline_ = nullptr;
    vk::raii::Buffer vertex_buffer_ = nullptr;
    vk::raii::DeviceMemory vertex_memory_ = nullptr;
    vk::raii::CommandPool command_pool_ = nullptr;
    std::vector<vk::raii::CommandBuffer> command_buffers_;
    std::vector<vk::raii::Framebuffer> framebuffers_;

    // Synchronization objects now managed by vkfw::VkFrameSync
    std::vector<Vertex> vertices_;
};

} // namespace ave::android
