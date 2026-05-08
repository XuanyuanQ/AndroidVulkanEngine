#pragma once

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
    bool createInstance();
    bool createSurface();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool loadSceneMesh();
    bool createVertexBuffer();
    bool createPipeline();
    bool createFramebuffers();
    bool createCommandPoolAndBuffers();
    bool createSyncObjects();
    void recordCommandBuffer(VkCommandBuffer command_buffer, uint32_t image_index);
    void drawFrame();

    void cleanupSurfaceResources();
    void cleanupDeviceResources();
    void logProjectAsset() const;
    std::vector<uint32_t> readShaderAsset(char const* path) const;
    std::string readTextAsset(char const* path) const;
    uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

    struct Vertex {
        float position[3];
        float color[4];
    };

    AAssetManager* assets_ = nullptr;
    std::string project_path_;
    ANativeWindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = UINT32_MAX;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_image_views_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;
    VkFence in_flight_ = VK_NULL_HANDLE;
    std::vector<Vertex> vertices_;
};

} // namespace ave::android
