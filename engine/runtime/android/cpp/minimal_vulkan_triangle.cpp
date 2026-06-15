#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"
#include "ave/render/RenderPasses.h"
#include "ave/render/RenderPassCommon.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/MaterialSystem.h"


#include <android/log.h>
#include <android/input.h>
#include <sys/system_properties.h>
#include "LogUtil.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>


namespace ave::android {

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr std::chrono::seconds kDebugSnapshotInterval{5};

bool ReadBoolSystemProperty(char const* name, bool fallback)
{
    char value[PROP_VALUE_MAX] = {};
    int const len = __system_property_get(name, value);
    if (len <= 0) {
        return fallback;
    }

    if (std::strcmp(value, "1") == 0 ||
        std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "TRUE") == 0 ||
        std::strcmp(value, "on") == 0 ||
        std::strcmp(value, "ON") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 ||
        std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "FALSE") == 0 ||
        std::strcmp(value, "off") == 0 ||
        std::strcmp(value, "OFF") == 0) {
        return false;
    }
    return fallback;
}

} // namespace

bool MinimalVulkanTriangle::create(AAssetManager* assets,
                                   std::string project_path,
                                   void* android_application_vm,
                                   void* android_application_context)
{
    assets_ = assets;
    project_path_ = std::move(project_path);
    android_application_vm_ = android_application_vm;
    android_application_context_ = android_application_context;
    logProjectAsset();
    return true;
}

bool MinimalVulkanTriangle::onTouchEvent(float x, float y, int32_t action, int32_t input_width, int32_t input_height, int32_t input_rotation)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    if (input_width > 0 && input_height > 0) {
        ui_runtime_.SetInputViewportSize(
            static_cast<uint32_t>(input_width),
            static_cast<uint32_t>(input_height),
            static_cast<uint32_t>(input_rotation));
    }

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        auto action_info = ui_runtime_.HandlePointerDown(x, y);
        if (!action_info.has_value()) {
            ui_touch_captured_ = false;
            return false;
        }
        ui_touch_captured_ = true;
        if (action_info->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
            Jni_TriggerScriptValueMethod(action_info->target, action_info->method, action_info->source_id, action_info->value);
        }
        return true;
    }

    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        bool const handled = ui_touch_captured_ || ui_runtime_.HandlePointerCancel().has_value();
        ui_touch_captured_ = false;
        return handled;
    }

    if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (ui_touch_captured_) {
            auto action_info = ui_runtime_.HandlePointerMove(x, y);
            if (action_info.has_value() && action_info->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
                Jni_TriggerScriptValueMethod(action_info->target, action_info->method, action_info->source_id, action_info->value);
            }
            return true;
        }
        auto action_info = ui_runtime_.HandlePointerMove(x, y);
        if (!action_info.has_value()) {
            return false;
        }
        if (action_info->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
            Jni_TriggerScriptValueMethod(action_info->target, action_info->method, action_info->source_id, action_info->value);
        }
        return true;
    }

    if (action != AMOTION_EVENT_ACTION_UP) {
        return false;
    }
    if (ui_touch_captured_) {
        auto action_info = ui_runtime_.HandlePointerUp(x, y);
        ui_touch_captured_ = false;
        if (!action_info.has_value()) {
            return true;
        }

        if (action_info->type == ave::ui::UIRuntime::ActionType::Click) {
            LOGI("UI click hit, trigger script target=%s method=%s", action_info->target.c_str(), action_info->method.c_str());
            Jni_TriggerScriptMethod(action_info->target, action_info->method);
        } else if (action_info->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
            Jni_TriggerScriptValueMethod(action_info->target, action_info->method, action_info->source_id, action_info->value);
        }
        return true;
    }
    auto action_info = ui_runtime_.HandlePointerUp(x, y);
    if (!action_info.has_value()) {
        return false;
    }

    if (action_info->type == ave::ui::UIRuntime::ActionType::Click) {
        Jni_TriggerScriptMethod(action_info->target, action_info->method);
    } else if (action_info->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
        Jni_TriggerScriptValueMethod(action_info->target, action_info->method, action_info->source_id, action_info->value);
    }
    return true;
}

void MinimalVulkanTriangle::logRuntimeSnapshot(char const* reason) const
{
    auto const& resource_system = renderer_.GetResourceSystem();
    auto const& pipeline_system = renderer_.GetPipelineSystem();
    LOGI("RuntimeSnapshot[%s]: meshes=%zu textures=%zu shaders=%zu materials=%zu set_layouts=%zu pipeline_layouts=%zu pipelines=%zu descriptor_sets=%zu free_descriptor_ids=%zu swapchain_images=%u frame_index=%u sync_frame_index=%u",
         reason != nullptr ? reason : "unknown",
         resource_system.GetMeshManager().MeshCount(),
         resource_system.GetTextureManager().TextureCount(),
         resource_system.GetShaderManager().ShaderCount(),
         resource_system.GetMaterialManager().MaterialCount(),
         pipeline_system.DescriptorSetLayoutCount(),
         pipeline_system.PipelineLayoutCount(),
         pipeline_system.PipelineCount(),
         pipeline_system.AllocatedDescriptorSetCount(),
         pipeline_system.FreeDescriptorSetCount(),
         swapchainWrap_.ImageCount(),
         frame_index_,
         sync_frame_index_);
}

void MinimalVulkanTriangle::setObjectPosition(std::string const& object_id, float x, float y, float z)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    glm::vec3 const position{x, y, z};
    if (scene_world_.SetObjectPosition(object_id, position)) {
        // LOGI("setObjectPosition scene object=%s position=(%.2f, %.2f, %.2f)",
        //      object_id.c_str(),
        //      x,
        //      y,
        //      z);
    } else if (!ui_runtime_.SetObjectPosition(object_id, position)) {
        // LOGW("setObjectPosition failed, object not found: %s", object_id.c_str());
    } else {
        // LOGI("setObjectPosition ui object=%s position=(%.2f, %.2f, %.2f)",
        //      object_id.c_str(),
        //      x,
        //      y,
        //      z);
    }
}

void MinimalVulkanTriangle::setObjectRotation(std::string const& object_id, float x, float y, float z)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    glm::vec3 const rotation{x, y, z};
    if (!scene_world_.SetObjectRotation(object_id, rotation)) {
        ui_runtime_.SetObjectRotation(object_id, rotation);
    }
}

void MinimalVulkanTriangle::setObjectScale(std::string const& object_id, float x, float y, float z)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    glm::vec3 const scale{x, y, z};
    if (!scene_world_.SetObjectScale(object_id, scale)) {
        ui_runtime_.SetObjectScale(object_id, scale);
    }
}

void MinimalVulkanTriangle::setObjectVisible(std::string const& object_id, bool visible)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    bool const scene_updated = scene_world_.SetObjectVisible(object_id, visible);
    bool const ui_updated = ui_runtime_.SetObjectVisible(object_id, visible);
    if (!scene_updated && !ui_updated) {
        LOGW("setObjectVisible failed, object not found: %s", object_id.c_str());
    }
}

void MinimalVulkanTriangle::setObjectColor(std::string const& object_id, float r, float g, float b, float a)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    glm::vec4 const color{r, g, b, a};
    if (!scene_world_.SetObjectColor(object_id, color)) {
        ui_runtime_.SetObjectColor(object_id, color);
    }
}

void MinimalVulkanTriangle::setObjectTexture(std::string const& object_id, std::string const& texture_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    if (!ui_runtime_.SetObjectTexture(object_id, texture_id)) {
        LOGW("setObjectTexture failed, UI object not found or not textured: %s", object_id.c_str());
    }
}

void MinimalVulkanTriangle::setObjectText(std::string const& object_id, std::string const& text)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    if (!ui_runtime_.SetObjectText(object_id, text)) {
        LOGW("setObjectText failed, UI text object not found: %s", object_id.c_str());
    }
}

void MinimalVulkanTriangle::setObjectProgress(std::string const& object_id, float value)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    if (!ui_runtime_.SetObjectProgress(object_id, value)) {
        LOGW("setObjectProgress failed, UI progress bar not found: %s", object_id.c_str());
    }
}

void MinimalVulkanTriangle::registerFontAtlas(int width, int height, void const* pixel_data)
{
    renderer_.GetResourceSystem().GetTextureManager().LoadTextureFromData(
        "__ave_font_atlas",
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        pixel_data,
        1
    );
}

std::string MinimalVulkanTriangle::instantiatePrefab(std::string const& prefab_path, std::string const& parent_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);

    // 1. Read the prefab XML text
    std::string const prefab_text = readTextAsset(prefab_path.c_str());
    if (prefab_text.empty()) {
        LOGE("Failed to read prefab XML asset from path: %s", prefab_path.c_str());
        return "";
    }

    // 2. Parse the prefab XML using XmlSceneLoader
    ave::project::XmlSceneLoader loader;
    loader.SetTextAssetLoader([this](std::string const& path) {
        return readTextAsset(path.c_str());
    });

    ave::project::PrefabDocument prefab;
    try {
        prefab = loader.LoadPrefabText(prefab_text);
    } catch (std::exception const& e) {
        LOGE("Failed to parse prefab XML from path %s: %s", prefab_path.c_str(), e.what());
        return "";
    }

    // 3. Load all meshes and materials referenced in the prefab
    auto& mesh_manager = renderer_.GetResourceSystem().GetMeshManager();
    auto& material_system = renderer_.GetMaterialSystem();

    for (auto const& object : prefab.objects) {
        if (object.components.mesh_renderer.has_value()) {
            auto const& mesh = *object.components.mesh_renderer;
            if (!mesh.mesh.empty()) {
                uint32_t mesh_id = mesh_manager.LoadMesh(mesh.mesh);
                if (mesh_id == 0) {
                    LOGE("Failed to load mesh resource %s during prefab instantiation", mesh.mesh.c_str());
                }
            }
            if (!mesh.material.empty()) {
                uint32_t mat_id = material_system.LoadMaterial(mesh.material);
                if (mat_id == 0) {
                    LOGE("Failed to load material resource %s during prefab instantiation", mesh.material.c_str());
                }
            }
        }
    }

    // 4. Delegate instantiation to SceneWorld (which clones, suffix-renames, and appends to the scene)
    size_t const before_object_count = scene_world_.GetSceneData().objects.size();
    std::string root_id = scene_world_.InstantiatePrefab(prefab, parent_id, renderer_.GetResourceSystem(), renderer_.GetMaterialSystem());
    if (root_id.empty()) {
        LOGE("Failed to instantiate prefab into SceneWorld");
        return "";
    }
    size_t const after_object_count = scene_world_.GetSceneData().objects.size();
    LOGI("Prefab instantiation complete: prefab=%s root_id=%s objects_before=%zu objects_after=%zu",
         prefab_path.c_str(),
         root_id.c_str(),
         before_object_count,
         after_object_count);

    // 5. Query the newly rebuilt scene and instantiate any new scripts
    project::SceneData const& active_scene = scene_world_.GetSceneData();
    for (auto const& object : active_scene.objects) {
        if (!object.components.script.has_value()) {
            continue;
        }
        if (instantiated_scripts_.count(object.id)) {
            continue;
        }

        auto const& script = *object.components.script;
        Jni_InstantiateScript(object.id, script.java_class, script.target_object, script.parameters);
        instantiated_scripts_.insert(object.id);
    }

    return root_id;
}

std::string MinimalVulkanTriangle::instantiatePrefab(std::string const& prefab_path,
                                                     std::string const& parent_id,
                                                     float x,
                                                     float y,
                                                     float z)
{
    std::string const root_id = instantiatePrefab(prefab_path, parent_id);
    if (root_id.empty()) {
        return "";
    }

    setObjectPosition(root_id, x, y, z);
    LOGI("Prefab instantiation positioned: prefab=%s root_id=%s position=(%.2f, %.2f, %.2f)",
         prefab_path.c_str(),
         root_id.c_str(),
         x,
         y,
         z);
    return root_id;
}

bool MinimalVulkanTriangle::destroyObject(std::string const& object_id)
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    if (std::find(pending_destructions_.begin(), pending_destructions_.end(), object_id) == pending_destructions_.end()) {
        pending_destructions_.push_back(object_id);
    }
    return true;
}

void MinimalVulkanTriangle::processPendingDestructions()
{
    if (pending_destructions_.empty()) {
        return;
    }

    std::vector<std::string> to_destroy = std::move(pending_destructions_);
    pending_destructions_.clear();

    bool scene_changed = false;
    for (auto const& object_id : to_destroy) {
        std::vector<std::string> destroyed_ids = scene_world_.DestroyObject(object_id, renderer_.GetResourceSystem(), renderer_.GetMaterialSystem());
        if (!destroyed_ids.empty()) {
            scene_changed = true;
            for (auto const& id : destroyed_ids) {
                Jni_DestroyScript(id);
                instantiated_scripts_.erase(id);
            }
        }
    }

    if (scene_changed) {
        ui_runtime_.RebuildFromScene(scene_world_.GetSceneData());
    }
}

bool MinimalVulkanTriangle::getObjectPosition(std::string const& object_id, glm::vec3& out_position) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return scene_world_.GetObjectPosition(object_id, out_position) ||
           ui_runtime_.GetObjectPosition(object_id, out_position);
}

bool MinimalVulkanTriangle::getObjectRotation(std::string const& object_id, glm::vec3& out_rotation) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return scene_world_.GetObjectRotation(object_id, out_rotation) ||
           ui_runtime_.GetObjectRotation(object_id, out_rotation);
}

bool MinimalVulkanTriangle::getObjectScale(std::string const& object_id, glm::vec3& out_scale) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return scene_world_.GetObjectScale(object_id, out_scale) ||
           ui_runtime_.GetObjectScale(object_id, out_scale);
}

bool MinimalVulkanTriangle::getObjectVisible(std::string const& object_id, bool& out_visible) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return scene_world_.GetObjectVisible(object_id, out_visible) ||
           ui_runtime_.GetObjectVisible(object_id, out_visible);
}

bool MinimalVulkanTriangle::getObjectColor(std::string const& object_id, glm::vec4& out_color) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return scene_world_.GetObjectColor(object_id, out_color) ||
           ui_runtime_.GetObjectColor(object_id, out_color);
}

bool MinimalVulkanTriangle::getObjectTexture(std::string const& object_id, std::string& out_texture_id) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return ui_runtime_.GetObjectTexture(object_id, out_texture_id);
}

bool MinimalVulkanTriangle::getObjectProgress(std::string const& object_id, float& out_value) const
{
    std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
    return ui_runtime_.GetObjectProgress(object_id, out_value);
}

void MinimalVulkanTriangle::destroy()
{
    stopRenderThread();
    cleanupSurfaceResources(true);
    Jni_ClearScripts();
    releaseWindow();
    app_initialized_ = false;
    scene_loaded_ = false;
    model_mesh_id_ = 0;
    use_frame_data_path_ = false;
    {
        std::lock_guard<std::mutex> lock(m_surface_mutex);
        m_surface_changed = false;
    }
}

void MinimalVulkanTriangle::setSurface(ANativeWindow* window)
{
    stopRenderThread();
    cleanupSurfaceResources(false);
    releaseWindow();
    window_ = window;
    if (window_ != nullptr) {
        ANativeWindow_acquire(window_);
        width_ = ANativeWindow_getWidth(window_);
        height_ = ANativeWindow_getHeight(window_);
    }

    if (window_ == nullptr) {
        return;
    }

    if (!initializeSurfaceResources()) {
        LOGE("Failed to initialize surface resources.");
        cleanupSurfaceResources(false);
        releaseWindow();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_surface_mutex);
        m_surface_changed = false;
    }
    m_running = true;
    m_render_thread = std::thread(&MinimalVulkanTriangle::drawFrame, this);
}

void MinimalVulkanTriangle::clearSurface()
{
    stopRenderThread();
    cleanupSurfaceResources(false);
    releaseWindow();
    {
        std::lock_guard<std::mutex> lock(m_surface_mutex);
        m_surface_changed = false;
    }
}

void MinimalVulkanTriangle::setForeground(bool foreground)
{
    m_foreground = foreground;
}

void MinimalVulkanTriangle::resize(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    if (width_ == width && height_ == height) {
        return;
    }

    width_ = width;
    height_ = height;
    std::lock_guard<std::mutex> lock(m_surface_mutex);
    m_surface_changed = true;
}

bool MinimalVulkanTriangle::loadSceneMesh()
{
    model_mesh_id_ = 0;
    frame_data_ = {};

    ave::project::XmlSceneLoader loader;
    loader.SetTextAssetLoader([this](std::string const& path) {
        return readTextAsset(path.c_str());
    });
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);
    auto& mesh_manager = renderer_.GetResourceSystem().GetMeshManager();
    Jni_ClearScripts();
    instantiated_scripts_.clear();
    for (auto const& object : scene.objects) {
        if (!object.components.mesh_renderer.has_value()) {
            continue;
        }

        auto const& mesh = *object.components.mesh_renderer;
        if (!mesh.mesh.empty()) {
            model_mesh_id_ = mesh_manager.LoadMesh(mesh.mesh);
            if (model_mesh_id_ == 0) {
                LOGE("Failed to load mesh resource %s", mesh.mesh.c_str());
                return false;
            }
            
            if (!mesh.material.empty()) {
                uint32_t mat_id = renderer_.GetMaterialSystem().LoadMaterial(mesh.material);
                if (mat_id != 0) {
                    auto const* mat = renderer_.GetMaterialSystem().GetMaterial(mat_id);
                } else {
                    LOGE("Failed to load material for %s", mesh.material.c_str());
                }
            }
        }
    }

    if (model_mesh_id_ == 0) {
        LOGE("Scene XML must define at least one valid external mesh.");
        return false;
    }

    auto const extent = swapchainWrap_.Extent();
    // Android Pre-rotation: ANativeWindow åœ¨ç«–å±æ‰‹æœºä¸Šè¿”å›žçš„æ˜¯æ—‹è½¬åŽçš„æ¨ªå±å°ºå¯¸ï¼ˆå®½>é«˜ï¼‰ï¼Œ
    // å®žé™…æ¸²æŸ“éœ€è¦ç”¨ height/width å¾—åˆ°æ­£ç¡®çš„ç«–å± aspect ratioã€‚
    float const aspect = (extent.width > 0)
        ? static_cast<float>(extent.height) / static_cast<float>(extent.width)
        : 9.0f / 16.0f;
    scene_world_.RebuildFromScene(scene, renderer_.GetResourceSystem(), renderer_.GetMaterialSystem(), aspect);
    ui_runtime_.SetViewportSize(extent.width, extent.height);
    ui_runtime_.RebuildFromScene(scene);

    for (auto const& object : scene.objects) {
        if (!object.components.script.has_value()) {
            continue;
        }
        if (instantiated_scripts_.count(object.id)) {
            continue;
        }

        auto const& script = *object.components.script;
        Jni_InstantiateScript(object.id, script.java_class, script.target_object, script.parameters);
        instantiated_scripts_.insert(object.id);
    }

    scene_world_.BuildFrameData(frame_index_, frame_data_);
    ui_runtime_.BuildFrameUi(frame_data_.ui_items);



    return true;
}

void MinimalVulkanTriangle::drawFrame()
{
    JNIEnv* env = nullptr;
    JavaVM* jvm = GetJavaVM();
    bool attached = false;
    if (jvm) {
        jint res = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (res == JNI_EDETACHED && jvm->AttachCurrentThread(&env, nullptr) == 0) {
            attached = true;
        }
    }

    auto last_time = std::chrono::high_resolution_clock::now();
    auto last_debug_snapshot = std::chrono::steady_clock::now();
    bool logged_background_pause = false;

    while (m_running) {
        if (!m_foreground.load()) {
            logged_background_pause = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (logged_background_pause) {
            logged_background_pause = false;
        }

        bool surface_changed = false;
        {
            std::lock_guard<std::mutex> lock(m_surface_mutex);
            if (m_surface_changed) {
                surface_changed = true;
                m_surface_changed = false;
            }
        }
        if (surface_changed && !recreateSwapchainResources()) {
            LOGE("drawFrame failed to recreate swapchain resources");
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        auto current_time = std::chrono::high_resolution_clock::now();
        float delta_time = std::chrono::duration<float>(current_time - last_time).count();
        last_time = current_time;
        if (delta_time > 0.1f) {
            delta_time = 0.1f;
        }

        if (use_frame_data_path_) {
            {
                std::lock_guard<std::recursive_mutex> lock(m_scene_mutex);
                Jni_UpdateScripts(delta_time);
                processPendingDestructions();
                scene_world_.BuildFrameData(frame_index_, frame_data_);
                ui_runtime_.Update(delta_time);
                ui_runtime_.BuildFrameUi(frame_data_.ui_items);
            }

            ave::render::FrameGraphRenderResult render_result = ave::render::FrameGraphRenderResult::Success;
            bool xr_frame_rendered = false;
            if (openxr_render_backend_.HasGraphics() && openxr_render_backend_.HasSwapchain()) {
                ave::xr::OpenXRRenderBackend::FrameTargets targets{};
                targets.frame = &frame_data_;
                targets.vk = &ctx_;
                openxr_render_backend_.SetNextFrameTargets(std::move(targets));
                ave::render::RenderFrameRequest xr_request{};
                auto const xr_begin_result = openxr_render_backend_.BeginFrame(xr_request);
                if (xr_begin_result == ave::render::FrameGraphRenderResult::Success) {
                    render_result = renderer_.RenderFrameGraphToTargets(xr_request);
                    render_result = openxr_render_backend_.EndFrame(render_result);
                    xr_frame_rendered = true;
                }
            }
            if (!xr_frame_rendered) {
                render_result =
                    renderer_.RenderFrameGraphFrame(frame_data_, ctx_, swapchainWrap_, sync_, sync_frame_index_);
            }
            if (render_result == ave::render::FrameGraphRenderResult::SwapchainOutOfDate) {
                std::lock_guard<std::mutex> lock(m_surface_mutex);
                m_surface_changed = true;
            }
            frame_index_++;
        }

        auto const now = std::chrono::steady_clock::now();
        if (now - last_debug_snapshot >= kDebugSnapshotInterval) {
            last_debug_snapshot = now;
            logRuntimeSnapshot("periodic");
        }
    }

    if (jvm && attached) {
        jvm->DetachCurrentThread();
    }
}
void MinimalVulkanTriangle::cleanupSurfaceResources(bool full_cleanup)
{
    if (!ctx_.IsInitialized()) {
        return;
    }

    try {
        if (ctx_.Device() != nullptr) {
            ctx_.Device().waitIdle();
        }
    } catch (...) {
    }

    try {
        openxr_render_backend_.ShutdownFrameResources(ctx_);
    } catch (...) {
    }

    try {
        renderer_.Shutdown();
    } catch (...) {
    }

    try {
        openxr_render_backend_.ShutdownGraphics(openxr_runtime_);
    } catch (...) {
    }

    try {
        openxr_runtime_.Shutdown();
    } catch (...) {
    }

    try {
        swapchainWrap_.Shutdown(ctx_);
    } catch (...) {
    }

    try {
        sync_.Shutdown(ctx_);
    } catch (...) {
    }

    if (full_cleanup) {
        try {
            ave::render::detail::ResetCommonSampler();
        } catch (...) {
        }
        try {
            ave::render::detail::ResetShadowSampler();
        } catch (...) {
        }
        try {
            renderer_.GetResourceSystem().Clear();
        } catch (...) {
        }
        try {
            ctx_.Shutdown();
        } catch (...) {
        }
    } else if (ctx_.IsInitialized()) {
        try {
            ctx_.SetWindow(nullptr);
        } catch (...) {
        }
    }
}

bool MinimalVulkanTriangle::initializeSurfaceResources()
{
    if (window_ == nullptr) {
        return false;
    }

    bool const first_time_init = !app_initialized_;
    bool const created_context = !ctx_.IsInitialized();
    bool const enable_openxr = ReadBoolSystemProperty("debug.ave.openxr", false);
    LOGI("OpenXR optional startup: property debug.ave.openxr enabled=%d", enable_openxr ? 1 : 0);
    bool xr_graphics_ready = false;
    if (enable_openxr) {
        if (!openxr_runtime_.Initialize(ave::xr::OpenXRRuntimeConfig{
                .enabled = enable_openxr,
                .android_application_vm = android_application_vm_,
                .android_application_context = android_application_context_,
            })) {
            LOGW("OpenXRRuntime initialization failed; continuing Android surface rendering");
        } else if (openxr_runtime_.IsInitialized() && openxr_render_backend_.InitializeGraphics(openxr_runtime_)) {
            if (created_context) {
                if (!ctx_.InitExternal(vkfw::ExternalVulkanContextCreateInfo{
                        .instance = static_cast<VkInstance>(openxr_render_backend_.VulkanInstanceHandle()),
                        .physical_device = static_cast<VkPhysicalDevice>(openxr_render_backend_.VulkanPhysicalDeviceHandle()),
                        .device = static_cast<VkDevice>(openxr_render_backend_.VulkanDeviceHandle()),
                        .graphics_queue_family_index = openxr_render_backend_.GraphicsQueueFamilyIndex(),
                        .supports_dynamic_rendering = openxr_render_backend_.SupportsDynamicRendering(),
                        .uses_core_dynamic_rendering = false,
                    })) {
                    LOGW("OpenXR external Vulkan context initialization failed; continuing Android surface rendering");
                }
            }
            xr_graphics_ready = ctx_.IsInitialized() && openxr_render_backend_.HasSwapchain();
        } else {
            LOGW("OpenXRRenderBackend graphics initialization failed; continuing Android surface rendering");
        }
    }

    if (created_context && !ctx_.IsInitialized()) {
        vkfw::ContextCreateInfo ci{};
        ci.window = window_;
#ifdef NDEBUG
        ci.enable_validation = false;
#else
        ci.enable_validation = false;
#endif
        if (!ctx_.Init(ci)) {
            return false;
        }
    } else if (!created_context && !xr_graphics_ready) {
        ctx_.SetWindow(window_);
    }
    xr_graphics_ready = xr_graphics_ready && ctx_.IsInitialized();

    renderer_.SetVkContext(&ctx_);
    renderer_.GetResourceSystem().GetMeshManager().SetTextAssetLoader(
        [this](std::string const& path) {
            return readTextAsset(path.c_str());
        });
    renderer_.GetMaterialSystem().SetTextAssetLoader(
        [this](std::string const& path) {
            return readTextAsset(path.c_str());
        });
    renderer_.GetResourceSystem().GetTextureManager().SetBinaryAssetLoader(
        [this](std::string const& path) {
            return readBinaryAsset(path.c_str());
        });
    renderer_.GetMaterialSystem().SetShaderAssetLoader(
        [this](std::string const& path) -> std::vector<uint32_t> {
            return readShaderAsset(path.c_str());
        });
    renderer_.GetResourceSystem().GetShaderManager().SetShaderAssetLoader(
        [this](std::string const& path) -> std::vector<uint32_t> {
            return readShaderAsset(path.c_str());
        });

    if (xr_graphics_ready) {
        if (!openxr_render_backend_.InitializeFrameResources(ctx_)) {
            LOGW("OpenXR frame resources initialization failed; continuing Android surface rendering");
            xr_graphics_ready = false;
        }
    }

    if (!xr_graphics_ready && sync_.FramesInFlight() == 0) {
        if (!sync_.Init(ctx_, kFramesInFlight)) {
            return false;
        }
    }

    if (!xr_graphics_ready && swapchainWrap_.ImageCount() == 0) {
        vkfw::SwapchainInfo si{};
        si.width = static_cast<uint32_t>(std::max(width_, 0));
        si.height = static_cast<uint32_t>(std::max(height_, 0));
        if (!swapchainWrap_.Init(ctx_, si)) {
            return false;
        }
    }
    if (!xr_graphics_ready) {
        sync_.EnsureRenderFinishedSize(ctx_, swapchainWrap_.ImageCount());
    }

    if (!scene_loaded_) {
        if (!loadSceneMesh()) {
            LOGE("Failed to load scene mesh.");
            return false;
        }
        scene_loaded_ = true;
        logRuntimeSnapshot("after_loadSceneMesh");
    }
    use_frame_data_path_ = true;
    if (renderer_.Graph().PassCount() == 0) {
        renderer_.Graph().AddPass(std::make_unique<ave::render::ShadowPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::ComputePass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::DepthPrepass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::SkyboxPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::PBRPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::UIPass>());
    }
    if (!xr_graphics_ready && !renderer_.InitializeFrameGraphBackend(ctx_, swapchainWrap_, sync_)) {
        LOGE("Failed to initialize FrameGraph backend.");
        return false;
    }

    ave::render::detail::EnsureSharedEnvironmentMaps(
        ctx_,
        &renderer_.GetResourceSystem(),
        frame_data_.environment.clear_color,
        frame_data_.environment.ambient_color);
    logRuntimeSnapshot("after_ensureSharedEnvironmentMaps");

    {
        ave::render::RenderPassContext preload_context{};
        preload_context.frame = &frame_data_;
        preload_context.resources = &renderer_.GetResourceSystem();
        preload_context.pipelines = &renderer_.GetPipelineSystem();
        preload_context.vk = &ctx_;
        renderer_.Graph().Preload(preload_context);
    }

    sync_frame_index_ = 0;
    frame_index_ = 0;
    if (first_time_init) {
        Jni_GenerateFontAtlas();
    }
    app_initialized_ = true;
    logRuntimeSnapshot("after_initializeSurfaceResources");
    return true;
}

bool MinimalVulkanTriangle::recreateSwapchainResources()
{
    if (!ctx_.IsInitialized() || window_ == nullptr) {
        return false;
    }

    try {
        ctx_.Device().waitIdle();
        renderer_.ResetFrameGraphRuntimeState(ctx_);
        renderer_.ShutdownFrameGraphBackend();
        swapchainWrap_.Recreate(ctx_);
        sync_.EnsureRenderFinishedSize(ctx_, swapchainWrap_.ImageCount());
        if (!renderer_.InitializeFrameGraphBackend(ctx_, swapchainWrap_, sync_)) {
            LOGE("Failed to reinitialize FrameGraph backend after swapchain recreate.");
            return false;
        }

        auto const extent = swapchainWrap_.Extent();
        float const aspect = (extent.width > 0)
            ? static_cast<float>(extent.height) / static_cast<float>(extent.width)
            : 9.0f / 16.0f;
        scene_world_.SetAspectRatio(aspect);
        ui_runtime_.SetViewportSize(extent.width, extent.height);
        return true;
    } catch (...) {
        LOGE("Swapchain recreate failed with exception.");
        return false;
    }
}

void MinimalVulkanTriangle::stopRenderThread()
{
    m_running = false;
    if (m_render_thread.joinable()) {
        m_render_thread.join();
    }
}

void MinimalVulkanTriangle::releaseWindow()
{
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}


void MinimalVulkanTriangle::logProjectAsset() const
{
    if (assets_ == nullptr) {
        return;
    }

    AAsset* project = AAssetManager_open(assets_, project_path_.c_str(), AASSET_MODE_BUFFER);
    if (project != nullptr) {
        AAsset_close(project);
    } else {
        LOGW("Project asset not found: %s", project_path_.c_str());
    }
}

std::vector<uint32_t> MinimalVulkanTriangle::readShaderAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("Shader asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::vector<uint32_t> words((size + sizeof(uint32_t) - 1) / sizeof(uint32_t));
    int const read = AAsset_read(asset, words.data(), size);
    AAsset_close(asset);

    if (read < 0 || static_cast<size_t>(read) != size || size % sizeof(uint32_t) != 0) {
        LOGE("Invalid SPIR-V asset: %s", path);
        return {};
    }

    return words;
}

std::string MinimalVulkanTriangle::readTextAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("Text asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::string text(size, '\0');
    int const read = AAsset_read(asset, text.data(), size);
    AAsset_close(asset);
    if (read < 0) {
        return {};
    }
    text.resize(static_cast<size_t>(read));
    return text;
}

std::vector<std::uint8_t> MinimalVulkanTriangle::readBinaryAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("Binary asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::vector<std::uint8_t> bytes(size);
    int const read = AAsset_read(asset, bytes.data(), size);
    AAsset_close(asset);

    if (read < 0 || static_cast<size_t>(read) != size) {
        LOGE("Failed to read binary asset: %s", path);
        return {};
    }

    return bytes;
}

} // namespace ave::android


