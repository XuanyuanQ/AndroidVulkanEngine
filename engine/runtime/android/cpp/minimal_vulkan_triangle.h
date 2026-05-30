#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "ave/core/FrameData.h"
#include "ave/render/Renderer.h"
#include "ave/scene/SceneWorld.h"
#include "ave/ui/UIRuntime.h"

#include <android/asset_manager.h>
#include <android/native_window.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <jni.h>

namespace ave::android {

// JNI Scripting Bridge Helpers
JavaVM* GetJavaVM();
JNIEnv* GetJniEnv();
void Jni_InstantiateScript(std::string const& object_id,
                           std::string const& java_class,
                           std::string const& target_object_id,
                           std::unordered_map<std::string, std::string> const& parameters);
void Jni_UpdateScripts(float dt);
void Jni_TriggerScriptMethod(std::string const& target, std::string const& method);
void Jni_ClearScripts();
void Jni_GenerateFontAtlas();

class MinimalVulkanTriangle {
public:
    bool create(AAssetManager* assets, std::string project_path);
    void destroy();
    void setSurface(ANativeWindow* window);
    void clearSurface();
    void resize(int width, int height);
    void setKeyState(int32_t key_code, bool pressed);
    void onTouchEvent(float x, float y, int32_t action);
    void setObjectPosition(std::string const& object_id, float x, float y, float z);
    void setObjectRotation(std::string const& object_id, float x, float y, float z);
    void setObjectScale(std::string const& object_id, float x, float y, float z);
    void setObjectVisible(std::string const& object_id, bool visible);
    void setObjectColor(std::string const& object_id, float r, float g, float b, float a);
    void setObjectTexture(std::string const& object_id, std::string const& texture_id);
    void registerFontAtlas(int width, int height, void const* pixel_data);

    bool getObjectPosition(std::string const& object_id, glm::vec3& out_position) const;
    bool getObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const;
    bool getObjectScale(std::string const& object_id, glm::vec3& out_scale) const;
    bool getObjectVisible(std::string const& object_id, bool& out_visible) const;
    bool getObjectColor(std::string const& object_id, glm::vec4& out_color) const;
    bool getObjectTexture(std::string const& object_id, std::string& out_texture_id) const;

private:
    bool loadSceneMesh();
    void drawFrame();

    void cleanupSurfaceResources();
    void logProjectAsset() const;
    std::vector<uint32_t> readShaderAsset(char const* path) const;
    std::vector<std::uint8_t> readBinaryAsset(char const* path) const;
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
    uint32_t sync_frame_index_ = 0;
    ave::render::Renderer renderer_{};
    ave::scene::SceneWorld scene_world_{};
    ave::ui::UIRuntime ui_runtime_{};
    ave::core::FrameData frame_data_{};
    std::vector<ave::render::RasterColorVertex> vertices_{};
    uint32_t model_mesh_id_ = 0;
    bool use_frame_data_path_ = false;

private:

    // 线程控制变量
    std::thread m_render_thread;
    std::atomic<bool> m_running{false};
    
    // 保护 Surface 的互斥锁（因为 Java 线程会异步传进来 Surface）
    std::mutex m_surface_mutex;
    ANativeWindow* m_window{nullptr};
    bool m_surface_changed{false};
};

} // namespace ave::android
