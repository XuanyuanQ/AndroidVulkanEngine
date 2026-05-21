#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "ave/core/FrameData.h"
#include "ave/render/Renderer.h"
#include "ave/scene/SceneWorld.h"

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
    ave::render::Renderer renderer_{};
    ave::scene::SceneWorld scene_world_{};
    ave::core::FrameData frame_data_{};
    std::vector<ave::render::RasterColorVertex> vertices_{};
    uint32_t model_mesh_id_ = 0;
    bool use_frame_data_path_ = false;
};

} // namespace ave::android
