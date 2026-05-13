#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "VulkanRasterRenderer.hpp"

#include <android/asset_manager.h>
#include <android/native_window.h>

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
    bool loadSceneMesh();
    void drawFrame();

    void cleanupSurfaceResources();
    void logProjectAsset() const;
    std::vector<uint32_t> readShaderAsset(char const* path) const;
    std::string readTextAsset(char const* path) const;

    AAssetManager* assets_ = nullptr;
    std::string project_path_;
    ANativeWindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    vkfw::VkContext ctx_{};
    vkfw::VkSwapchain swapchainWrap_{};
    vkfw::VkFrameSync sync_{};
    uint32_t frame_index_ = 0;
    rhi::VulkanRasterRenderer renderer_{};
    std::vector<rhi::RasterColorVertex> vertices_{};
};

} // namespace ave::android
