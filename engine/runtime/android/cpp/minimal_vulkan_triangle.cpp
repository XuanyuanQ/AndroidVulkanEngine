#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"
#include "ave/render/RenderPasses.h"
#include "ave/resource/ResourceSystem.h"
#include "ave/render/MaterialSystem.h"

#include <android/log.h>
#include "LogUtil.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>


namespace ave::android {

namespace {

constexpr uint32_t kFramesInFlight = 2;
constexpr const char* kLogTag = "AveRuntime";



} // namespace

bool MinimalVulkanTriangle::create(AAssetManager* assets, std::string project_path)
{
    assets_ = assets;
    project_path_ = std::move(project_path);
    logProjectAsset();
    return true;
}
void MinimalVulkanTriangle::setKeyState(int32_t key_code, bool pressed){
        LOGI("Key event: code=%d, pressed=%s", key_code, pressed ? "true" : "false");
        scene_world_.g_key_states[key_code] = pressed;
    }

void MinimalVulkanTriangle::onTouchEvent(float x, float y, int32_t action)
{
    // Android MotionEvent.ACTION_UP = 1 最后一个手指离开屏幕的事件，才触发点击逻辑
    if (action != 1) {
        return;
    }

    auto action_info = ui_runtime_.HandlePointerUp(x, y);
    if (!action_info.has_value()) {
        return;
    }

    LOGI("UI click hit, trigger script target=%s method=%s", action_info->target.c_str(), action_info->method.c_str());
    Jni_TriggerScriptMethod(action_info->target, action_info->method);
}

void MinimalVulkanTriangle::setObjectPosition(std::string const& object_id, float x, float y, float z)
{
    scene_world_.SetObjectPosition(object_id, glm::vec3{x, y, z});
}

void MinimalVulkanTriangle::setObjectRotation(std::string const& object_id, float x, float y, float z)
{
    scene_world_.SetObjectRotation(object_id, glm::vec3{x, y, z});
}

void MinimalVulkanTriangle::setObjectScale(std::string const& object_id, float x, float y, float z)
{
    scene_world_.SetObjectScale(object_id, glm::vec3{x, y, z});
}

void MinimalVulkanTriangle::setObjectVisible(std::string const& object_id, bool visible)
{
    LOGI("setObjectVisible object_id=%s visible=%d", object_id.c_str(),visible);
    scene_world_.SetObjectVisible(object_id, visible);
}

void MinimalVulkanTriangle::setObjectColor(std::string const& object_id, float r, float g, float b, float a)
{
    scene_world_.SetObjectColor(object_id, glm::vec4{r, g, b, a});
}

bool MinimalVulkanTriangle::GetObjectVisible(std::string const& object_id, bool& out_visible){
    return scene_world_.GetObjectVisible(object_id,out_visible);
}

void MinimalVulkanTriangle::destroy()
{
    m_running = false;
    if (m_render_thread.joinable()) {
        m_render_thread.join();
    }
    Jni_ClearScripts();
    clearSurface();
    LOGI("Ave runtime destroyed.");
}

void MinimalVulkanTriangle::setSurface(ANativeWindow* window)
{
    LOGI("Ave runtime setSurface.");
    clearSurface();
    window_ = window;
    if (window_ != nullptr) {
        ANativeWindow_acquire(window_);
    }

    if (window_ == nullptr) {
        return;
    }

    vkfw::ContextCreateInfo ci{};
    ci.window = window_;
    #ifdef NDEBUG
    ci.enable_validation = false;
    #else
        ci.enable_validation = false;
    #endif
    ctx_.Init(ci);
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

    sync_.Init(ctx_, kFramesInFlight);

    vkfw::SwapchainInfo si{};
    swapchainWrap_.Init(ctx_, si);
    sync_.EnsureRenderFinishedSize(ctx_, swapchainWrap_.ImageCount());

    if (!loadSceneMesh()) {
        LOGE("Failed to load scene mesh.");
        return;
    }
    use_frame_data_path_ = true;
    if (renderer_.Graph().PassCount() == 0) {
        renderer_.Graph().AddPass(std::make_unique<ave::render::ShadowPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::ComputePass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::PBRPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::UIPass>());
    }
    if (!renderer_.InitializeFrameGraphBackend(ctx_, swapchainWrap_, sync_)) {
            LOGE("Failed to initialize FrameGraph backend.");
            return;
    }
    m_running = true;
    m_surface_changed = true; // 标记 Surface 发生了变化
    m_render_thread = std::thread(&MinimalVulkanTriangle::drawFrame, this);
}

void MinimalVulkanTriangle::clearSurface()
{
    // First clear the window reference
    if (window_ != nullptr) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
    
    // Then clear Vulkan resources only if context is properly initialized
    if (ctx_.IsInitialized())
    {
        try {
            // Check if device is valid before using it
            if (ctx_.Device() != nullptr) {
                ctx_.Device().waitIdle();
            }
            renderer_.ShutdownFrameGraphBackend();
            renderer_.ShutdownRaster();
            renderer_.GetResourceSystem().Clear();
            swapchainWrap_.Shutdown(ctx_);
            sync_.Shutdown(ctx_);
            ctx_.Shutdown();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }
    model_mesh_id_ = 0;
    use_frame_data_path_ = false;
}

void MinimalVulkanTriangle::resize(int width, int height)
{
    //需要考虑怎么重新建 swapchain 和相关资源，以及如何通知渲染线程进行调整
    // width_ = width;
    // height_ = height;
    // __android_log_print(ANDROID_LOG_INFO, kLogTag, "Surface resized: %dx%d", width_, height_);
    // if (ctx_.IsInitialized() && window_ != nullptr) {
    //     drawFrame();
    // }
}

bool MinimalVulkanTriangle::loadSceneMesh()
{
    vertices_.clear();
    model_mesh_id_ = 0;
    frame_data_ = {};

    ave::project::XmlSceneLoader loader;
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);
    auto& mesh_manager = renderer_.GetResourceSystem().GetMeshManager();
    auto& shader_manager = renderer_.GetResourceSystem().GetShaderManager();
    auto& material_manager = renderer_.GetResourceSystem().GetMaterialManager();
    uint32_t shader_id = 0;
    Jni_ClearScripts();
    for (auto const& object : scene.objects) {
        if (object.components.script.has_value()) {
            auto const& script = *object.components.script;
            LOGI("Instantiating Java script %s for GameObject %s", script.java_class.c_str(), object.id.c_str());
            Jni_InstantiateScript(object.id, script.java_class, script.target_object, script.parameters);
        }
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
                    LOGI("Parsed and registered logical material %s %swith color (%.2f, %.2f, %.2f, %.2f)", mesh.material.c_str(), mat->shader_name.c_str(), mat->params.base_color[0], mat->params.base_color[1], mat->params.base_color[2], mat->params.base_color[3]);
                } else {
                    LOGE("Failed to load material for %s", mesh.material.c_str());
                }
            }
            continue;
        }

        for (auto const& source : mesh.vertices) {
            ave::render::RasterColorVertex vertex{};
            vertex.position = source.position;
            vertex.color = source.color;
            vertices_.push_back(vertex);
        }
    }

    if (vertices_.size() < 3 && model_mesh_id_ == 0) {
        LOGE("Scene XML must define at least three <Vertex> entries or a valid external mesh.");
        return false;
    }

    auto const extent = swapchainWrap_.Extent();
    // Android Pre-rotation: ANativeWindow 在竖屏手机上返回的是旋转后的横屏尺寸（宽>高），
    // 实际渲染需要用 height/width 得到正确的竖屏 aspect ratio。
    float const aspect = (extent.width > 0)
        ? static_cast<float>(extent.height) / static_cast<float>(extent.width)
        : 9.0f / 16.0f;
    LOGI("Swapchain extent: %ux%u, aspect=%.4f", extent.width, extent.height, aspect);
    scene_world_.RebuildFromScene(scene, renderer_.GetResourceSystem(), renderer_.GetMaterialSystem(), aspect);
    ui_runtime_.SetViewportSize(extent.width, extent.height);
    ui_runtime_.RebuildFromScene(scene);
    scene_world_.BuildFrameData(frame_index_, frame_data_);
    ui_runtime_.BuildFrameUi(frame_data_.ui_items);



    LOGI("Loaded scene mesh data from %s (%zu inline preview vertices, model mesh id %u)", project.entry_scene.c_str(), vertices_.size(), model_mesh_id_);
    return true;
}

void MinimalVulkanTriangle::drawFrame()
{
    // Attach current thread to Java VM
    JNIEnv* env = nullptr;
    JavaVM* jvm = GetJavaVM();
    bool attached = false;
    if (jvm) {
        jint res = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (jvm->AttachCurrentThread(&env, nullptr) == 0) {
                attached = true;
            }
        }
    }

    auto last_time = std::chrono::high_resolution_clock::now();
    bool has_swapchain = false;

    while (m_running) {
        // 1. 处理来自 Java 线程的 Surface 变更
        {
            std::lock_guard<std::mutex> lock(m_surface_mutex);
            if (m_surface_changed) {
            }
        }

        // 3. 计算 delta_time
        auto current_time = std::chrono::high_resolution_clock::now();
        float delta_time = std::chrono::duration<float>(current_time - last_time).count();
        last_time = current_time;
        if (delta_time > 0.1f) delta_time = 0.1f; // 限制单帧最大时长
        // 4. 【核心更新】调用你的摄像机更新（它会自动读取 JNI 传进来的按键状态）
        if (use_frame_data_path_) {
            scene_world_.UpdateDebugLight(delta_time);
            scene_world_.UpdateDebugCamera(delta_time);
            Jni_UpdateScripts(delta_time); // Update scripts
            scene_world_.BuildFrameData(frame_index_, frame_data_);
            ui_runtime_.Update(delta_time);
            ui_runtime_.BuildFrameUi(frame_data_.ui_items);
            renderer_.RenderFrameGraphFrame(frame_data_, ctx_, swapchainWrap_, sync_, sync_frame_index_);
            frame_index_++;
            // No early return; continue looping to process next frame
        } else {
            // Existing non-frame-data path logic (if any) can be placed here
        }
    }

    if (jvm && attached) {
        jvm->DetachCurrentThread();
    }
}

void MinimalVulkanTriangle::cleanupSurfaceResources()
{
    renderer_.ShutdownRaster();
}


void MinimalVulkanTriangle::logProjectAsset() const
{
    if (assets_ == nullptr) {
        return;
    }

    AAsset* project = AAssetManager_open(assets_, project_path_.c_str(), AASSET_MODE_BUFFER);
    if (project != nullptr) {
        LOGI("Loaded project asset: %s (%ld bytes)", project_path_.c_str(), static_cast<long>(AAsset_getLength(project)));
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
