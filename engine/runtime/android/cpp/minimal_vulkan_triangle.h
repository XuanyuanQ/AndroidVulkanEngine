#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "ave/core/FrameData.h"
#include "ave/render/Renderer.h"
#include "ave/scene/SceneWorld.h"
#include "ave/ui/UIRuntime.h"
#include "ave/xr/OpenXRRenderBackend.h"
#include "ave/xr/OpenXRRuntime.h"

#include <android/asset_manager.h>
#include <android/native_window.h>

#include <string>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
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
void Jni_TriggerScriptValueMethod(std::string const& target, std::string const& method, std::string const& source_id, float value);
void Jni_ClearScripts();
void Jni_GenerateFontAtlas();
void Jni_DestroyScript(std::string const& object_id);

class MinimalVulkanTriangle {
public:
    bool create(AAssetManager* assets, std::string project_path, void* android_application_vm, void* android_application_context);
    void destroy();
    void setSurface(ANativeWindow* window);
    void clearSurface();
    void setForeground(bool foreground);
    void resize(int width, int height);
    bool onTouchEvent(float x, float y, int32_t action, int32_t input_width, int32_t input_height, int32_t input_rotation);
    void setObjectPosition(std::string const& object_id, float x, float y, float z);
    void setObjectRotation(std::string const& object_id, float x, float y, float z);
    void setObjectScale(std::string const& object_id, float x, float y, float z);
    void setObjectVisible(std::string const& object_id, bool visible);
    void setObjectColor(std::string const& object_id, float r, float g, float b, float a);
    void setObjectTexture(std::string const& object_id, std::string const& texture_id);
    void setObjectText(std::string const& object_id, std::string const& text);
    void setObjectProgress(std::string const& object_id, float value);
    void registerFontAtlas(int width, int height, void const* pixel_data);
    std::string instantiatePrefab(std::string const& prefab_path, std::string const& parent_id);
    std::string instantiatePrefab(std::string const& prefab_path, std::string const& parent_id, float x, float y, float z);
    bool destroyObject(std::string const& object_id);

    bool getObjectPosition(std::string const& object_id, glm::vec3& out_position) const;
    bool getObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const;
    bool getObjectScale(std::string const& object_id, glm::vec3& out_scale) const;
    bool getObjectVisible(std::string const& object_id, bool& out_visible) const;
    bool getObjectColor(std::string const& object_id, glm::vec4& out_color) const;
    bool getObjectTexture(std::string const& object_id, std::string& out_texture_id) const;
    bool getObjectProgress(std::string const& object_id, float& out_value) const;

private:
    bool loadSceneMesh();
    bool initializeSurfaceResources();
    bool recreateSwapchainResources();
    void stopRenderThread();
    void releaseWindow();
    void drawFrame();
    void logRuntimeSnapshot(char const* reason) const;

    void cleanupSurfaceResources(bool full_cleanup);
    void logProjectAsset() const;
    std::vector<uint32_t> readShaderAsset(char const* path) const;
    std::vector<std::uint8_t> readBinaryAsset(char const* path) const;
    std::string readTextAsset(char const* path) const;

    AAssetManager* assets_ = nullptr;
    std::string project_path_;
    void* android_application_vm_ = nullptr;
    void* android_application_context_ = nullptr;
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
    ave::xr::OpenXRRuntime openxr_runtime_{};
    ave::xr::OpenXRRenderBackend openxr_render_backend_{};
    ave::core::FrameData frame_data_{};
    uint32_t model_mesh_id_ = 0;
    bool use_frame_data_path_ = false;
    bool app_initialized_ = false;
    bool scene_loaded_ = false;

private:

    // 线程控制变量
    std::thread m_render_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_foreground{true};
    
    // 保护 Surface 的互斥锁（因为 Java 线程会异步传进来 Surface）
    std::mutex m_surface_mutex;
    bool m_surface_changed{false};

    // 保护场景树与 UI 的递归锁，防并发访问崩溃
    mutable std::recursive_mutex m_scene_mutex;
    std::unordered_set<std::string> instantiated_scripts_;
    bool ui_touch_captured_{false};

    void processPendingDestructions();
    std::vector<std::string> pending_destructions_;
};

} // namespace ave::android
