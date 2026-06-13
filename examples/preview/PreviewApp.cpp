#include "PreviewArgs.h"
#include "PreviewConfig.h"
#include "PreviewNativeEntry.h"
#include "PreviewUtils.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"
#include "ave/core/FrameData.h"
#include "ave/core/JobSystem.h"
#include "ave/project/XmlSceneLoader.h"
#include "ave/render/RenderPasses.h"
#include "ave/render/Renderer.h"
#include "ave/scene/SceneWorld.h"
#include "ave/ui/UIRuntime.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#include <cstdlib>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using namespace ave_preview;

class PreviewScriptHost : public PreviewNativeCallbacks {
public:
    ~PreviewScriptHost()
    {
        java_runtime_.Stop();
    }

    void SetJavaClassDir(std::filesystem::path class_dir)
    {
        java_class_dir_ = std::move(class_dir);
    }

    void Bind(ave::project::SceneData const& scene,
              ave::scene::SceneWorld* scene_world,
              ave::ui::UIRuntime* ui_runtime,
              ave::resource::ResourceSystem* resources,
              ave::render::MaterialSystem* materials,
              std::filesystem::path project_dir)
    {
        scene_world_ = scene_world;
        ui_runtime_ = ui_runtime;
        resources_ = resources;
        materials_ = materials;
        project_dir_ = std::move(project_dir);
        java_id_to_runtime_id_.clear();
        bool const java_ready = java_runtime_.Start(java_class_dir_, this);
        if (java_ready) {
            SendJava(MakeProtocolLine("reload", java_class_dir_.string()));
            SendJavaScene(scene);
        }
    }

    void Update(float delta_time)
    {
        if (!java_runtime_.IsActive()) {
            return;
        }
        SendJava(MakeProtocolLine("update", std::to_string(delta_time)));
        PollJavaOutput();
    }

    void DispatchClick(std::string const& target, std::string const& method)
    {
        if (!java_runtime_.IsActive()) {
            return;
        }
        SendJava(MakeProtocolLine("click", target, method));
        PollJavaOutput();
    }

    void DispatchValueChanged(std::string const& target, std::string const& method, std::string const& source_id, float value)
    {
        if (!java_runtime_.IsActive()) {
            return;
        }
        SendJava(MakeProtocolLine("value", target, method, source_id, std::to_string(value)));
        PollJavaOutput();
    }

    void DispatchPointerDown(double x, double y)
    {
        SendPointerEvent(0, x, y);
    }

    void DispatchPointerMove(double x, double y)
    {
        SendPointerEvent(2, x, y);
    }

    void DispatchPointerUp()
    {
        SendPointerEvent(1, last_pointer_x_, last_pointer_y_);
    }

private:
    void SendJava(std::string const& line)
    {
        java_runtime_.Send(line);
    }

    void PollJavaOutput()
    {
    }

    void SendPointerEvent(int action, double x, double y)
    {
        if (!java_runtime_.IsActive()) {
            return;
        }
        last_pointer_x_ = x;
        last_pointer_y_ = y;
        SendJava(MakeProtocolLine("touch",
                                  std::to_string(action),
                                  std::to_string(static_cast<float>(x)),
                                  std::to_string(static_cast<float>(y))));
        PollJavaOutput();
    }

    void SendJavaScene(ave::project::SceneData const& scene)
    {
        SendJava("clear\n");
        for (auto const& object : scene.objects) {
            glm::vec3 position{0.0f};
            glm::vec3 rotation{0.0f};
            glm::vec3 scale{1.0f};
            if (object.components.transform.has_value()) {
                position = object.components.transform->position;
                rotation = object.components.transform->rotation;
                scale = object.components.transform->scale;
            }
            SendJava(MakeProtocolLine("object",
                                      object.id,
                                      std::to_string(position.x),
                                      std::to_string(position.y),
                                      std::to_string(position.z),
                                      std::to_string(rotation.x),
                                      std::to_string(rotation.y),
                                      std::to_string(rotation.z),
                                      std::to_string(scale.x),
                                      std::to_string(scale.y),
                                      std::to_string(scale.z)));
        }
        for (auto const& object : scene.objects) {
            if (!object.components.script.has_value()) {
                continue;
            }
            SendJavaScript(object);
        }
        PollJavaOutput();
    }

    void SendJavaScript(ave::project::GameObjectData const& object)
    {
        if (!object.components.script.has_value()) {
            return;
        }
        auto const& script = *object.components.script;
        std::ostringstream line;
        line << "script|" << ProtocolEscape(object.id)
             << '|' << ProtocolEscape(script.java_class)
             << '|' << ProtocolEscape(script.target_object);
        for (auto const& [key, value] : script.parameters) {
            line << '|' << ProtocolEscape(key + "=" + value);
        }
        line << '\n';
        SendJava(line.str());
    }

    void OnJavaLog(std::string const& message) override
    {
        std::cout << "[java] " << message << "\n";
    }

    void OnJavaSetPosition(std::string const& object_id, float x, float y, float z) override
    {
        scene_world_->SetObjectPosition(ResolveJavaObjectId(object_id), {x, y, z});
    }

    void OnJavaSetRotation(std::string const& object_id, float x, float y, float z) override
    {
        scene_world_->SetObjectRotation(ResolveJavaObjectId(object_id), {x, y, z});
    }

    void OnJavaSetScale(std::string const& object_id, float x, float y, float z) override
    {
        scene_world_->SetObjectScale(ResolveJavaObjectId(object_id), {x, y, z});
    }

    void OnJavaSetVisible(std::string const& object_id, bool visible) override
    {
        std::string const resolved_id = ResolveJavaObjectId(object_id);
        scene_world_->SetObjectVisible(resolved_id, visible);
        ui_runtime_->SetObjectVisible(resolved_id, visible);
    }

    void OnJavaSetColor(std::string const& object_id, float r, float g, float b, float a) override
    {
        glm::vec4 color{r, g, b, a};
        std::string const resolved_id = ResolveJavaObjectId(object_id);
        scene_world_->SetObjectColor(resolved_id, color);
        ui_runtime_->SetObjectColor(resolved_id, color);
    }

    void OnJavaSetTexture(std::string const& object_id, std::string const& texture) override
    {
        ui_runtime_->SetObjectTexture(ResolveJavaObjectId(object_id), texture);
    }

    void OnJavaSetText(std::string const& object_id, std::string const& text) override
    {
        ui_runtime_->SetObjectText(ResolveJavaObjectId(object_id), text);
    }

    void OnJavaSetProgress(std::string const& object_id, float value) override
    {
        ui_runtime_->SetObjectProgress(ResolveJavaObjectId(object_id), value);
    }

    bool OnJavaDestroyObject(std::string const& object_id) override
    {
        return DestroyObjectFromJava(object_id);
    }

    void OnJavaInstantiatePrefab(std::string const& requested_id,
                                 std::string const& prefab_path,
                                 std::string const& parent_id,
                                 float x,
                                 float y,
                                 float z) override
    {
        InstantiatePrefabFromJava(requested_id, prefab_path, parent_id, x, y, z);
    }

    bool DestroyObjectFromJava(std::string const& requested_id)
    {
        if (scene_world_ == nullptr || resources_ == nullptr || materials_ == nullptr) {
            return false;
        }
        std::string const object_id = ResolveJavaObjectId(requested_id);
        auto destroyed = scene_world_->DestroyObject(object_id, *resources_, *materials_);
        if (destroyed.empty()) {
            std::cout << "[preview] Java destroy ignored, object not found: " << requested_id
                      << " resolved=" << object_id << "\n";
            return false;
        }
        for (auto const& id : destroyed) {
            java_id_to_runtime_id_.erase(id);
        }
        return true;
    }

    std::string ResolveJavaObjectId(std::string const& object_id) const
    {
        auto const found = java_id_to_runtime_id_.find(object_id);
        return found == java_id_to_runtime_id_.end() ? object_id : found->second;
    }

    void InstantiatePrefabFromJava(std::string const& requested_id,
                                   std::string const& prefab_path,
                                   std::string const& parent_id,
                                   float x,
                                   float y,
                                   float z)
    {
        if (scene_world_ == nullptr || resources_ == nullptr || materials_ == nullptr) {
            return;
        }
        ave::project::XmlSceneLoader loader;
        loader.SetTextAssetLoader([this](std::string const& path) {
            return ReadTextFile(ResolveProjectAsset(project_dir_, path));
        });
        auto const prefab_text = ReadTextFile(ResolveProjectAsset(project_dir_, prefab_path));
        auto const prefab = loader.LoadPrefabText(prefab_text);
        std::string const new_id = scene_world_->InstantiatePrefab(prefab, parent_id, *resources_, *materials_);
        if (!new_id.empty()) {
            java_id_to_runtime_id_[requested_id] = new_id;
            scene_world_->SetObjectPosition(new_id, {x, y, z});
            for (auto const& object : scene_world_->GetSceneData().objects) {
                if (object.id == new_id) {
                    if (java_runtime_.IsActive()) {
                        SendJavaScript(object);
                    }
                    break;
                }
            }
            if (new_id != requested_id) {
                std::cout << "[preview] Java prefab requested id " << requested_id << ", runtime id " << new_id << "\n";
            }
        }
    }

    ave::scene::SceneWorld* scene_world_ = nullptr;
    ave::ui::UIRuntime* ui_runtime_ = nullptr;
    ave::resource::ResourceSystem* resources_ = nullptr;
    ave::render::MaterialSystem* materials_ = nullptr;
    std::filesystem::path project_dir_;
    std::filesystem::path java_class_dir_;
    PreviewJavaRuntime java_runtime_{};
    std::unordered_map<std::string, std::string> java_id_to_runtime_id_;
    double last_pointer_x_ = 0.0;
    double last_pointer_y_ = 0.0;
};

class PreviewApp {
public:
    explicit PreviewApp(PreviewArgs args)
        : args_(std::move(args))
    {
    }

    int Run()
    {
        if (!std::filesystem::exists(args_.project_dir / "project.xml")) {
            std::cerr << "[preview] missing project.xml in " << args_.project_dir << "\n";
            return 2;
        }

        if (!glfwInit()) {
            std::cerr << "[preview] glfwInit failed\n";
            return 2;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window_ = glfwCreateWindow(static_cast<int>(args_.width),
                                   static_cast<int>(args_.height),
                                   "Ave Preview",
                                   nullptr,
                                   nullptr);
        if (window_ == nullptr) {
            glfwTerminate();
            std::cerr << "[preview] failed to create GLFW window\n";
            return 2;
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int width, int height) {
            auto* app = static_cast<PreviewApp*>(glfwGetWindowUserPointer(window));
            app->resize_pending_ = true;
            app->width_ = std::max(width, 1);
            app->height_ = std::max(height, 1);
        });
        glfwSetMouseButtonCallback(window_, [](GLFWwindow* window, int button, int action, int mods) {
            (void)mods;
            auto* app = static_cast<PreviewApp*>(glfwGetWindowUserPointer(window));
            if (app != nullptr) {
                app->OnMouseButton(button, action);
            }
        });
        glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double x, double y) {
            auto* app = static_cast<PreviewApp*>(glfwGetWindowUserPointer(window));
            if (app != nullptr) {
                app->OnCursorPos(x, y);
            }
        });
        glfwSetScrollCallback(window_, [](GLFWwindow* window, double x_offset, double y_offset) {
            (void)x_offset;
            auto* app = static_cast<PreviewApp*>(glfwGetWindowUserPointer(window));
            if (app != nullptr) {
                app->OnScroll(y_offset);
            }
        });

        try {
            InitializeRuntime();
            LoadProjectAndScene(true);
            file_stamps_.Refresh(args_.project_dir);
            MainLoop();
            ShutdownRuntime();
        } catch (std::exception const& exc) {
            std::cerr << "[preview] " << exc.what() << "\n";
            ShutdownRuntime();
            glfwDestroyWindow(window_);
            glfwTerminate();
            return 2;
        }

        glfwDestroyWindow(window_);
        glfwTerminate();
        return 0;
    }

private:
    std::filesystem::path JavaClassDir() const
    {
        return args_.project_dir / "build" / "preview" / ("java_classes_" + std::to_string(java_compile_generation_));
    }

    bool CompilePreviewJavaScripts()
    {
        ++java_compile_generation_;
        auto const class_dir = JavaClassDir();
        if (!ave_preview::CompilePreviewJavaScripts(args_.project_dir, class_dir)) {
            return false;
        }
        current_java_class_dir_ = class_dir;
        return true;
    }

    void InitializeRuntime()
    {
        jobs_.Start(0);
        bool const java_compiled = CompilePreviewJavaScripts();

        vkfw::ContextCreateInfo ci{};
        ci.window = window_;
        ci.enable_validation = false;
        if (!ctx_.Init(ci)) {
            throw std::runtime_error("failed to initialize Vulkan context");
        }

        renderer_.SetVkContext(&ctx_);
        renderer_.GetResourceSystem().GetMeshManager().SetTextAssetLoader([this](std::string const& path) {
            return ReadTextFile(ResolveProjectAsset(args_.project_dir, path));
        });
        renderer_.GetResourceSystem().GetTextureManager().SetBinaryAssetLoader([this](std::string const& path) {
            return ReadPreviewBinaryAsset(args_.project_dir, path);
        });
        auto shader_loader = [this](std::string const& path) {
            auto shader_path = std::filesystem::path(path);
            if (!args_.compiled_shader_dir.empty() && shader_path.is_relative()) {
                auto compiled = Normalize(args_.compiled_shader_dir / shader_path.filename());
                if (std::filesystem::exists(compiled)) {
                    return ReadSpirv(compiled);
                }
            }
            return ReadSpirv(ResolveProjectAsset(args_.project_dir, path));
        };
        renderer_.GetResourceSystem().GetShaderManager().SetShaderAssetLoader(shader_loader);
        renderer_.GetMaterialSystem().SetTextAssetLoader([this](std::string const& path) {
            return ReadTextFile(ResolveProjectAsset(args_.project_dir, path));
        });
        renderer_.GetMaterialSystem().SetShaderAssetLoader(shader_loader);

        if (!sync_.Init(ctx_, kFramesInFlight)) {
            throw std::runtime_error("failed to initialize frame sync");
        }

        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(window_, &fb_width, &fb_height);
        width_ = std::max(fb_width, 1);
        height_ = std::max(fb_height, 1);
        CreateSwapchainResources();

        renderer_.Graph().AddPass(std::make_unique<ave::render::ShadowPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::ComputePass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::DepthPrepass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::SkyboxPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::PBRPass>());
        renderer_.Graph().AddPass(std::make_unique<ave::render::UIPass>());
        script_host_.SetJavaClassDir(java_compiled ? current_java_class_dir_ : std::filesystem::path{});
    }

    void CreateSwapchainResources()
    {
        vkfw::SwapchainInfo si{};
        si.width = static_cast<uint32_t>(width_);
        si.height = static_cast<uint32_t>(height_);
        if (swapchain_.ImageCount() == 0) {
            if (!swapchain_.Init(ctx_, si)) {
                throw std::runtime_error("failed to initialize swapchain");
            }
        } else {
            swapchain_.Recreate(ctx_);
        }
        sync_.EnsureRenderFinishedSize(ctx_, swapchain_.ImageCount());
        if (!renderer_.InitializeFrameGraphBackend(ctx_, swapchain_, sync_)) {
            throw std::runtime_error("failed to initialize FrameGraph backend");
        }
    }

    void LoadProjectAndScene(bool reset_gpu_resources)
    {
        try {
            ave::project::XmlSceneLoader loader;
            loader.SetTextAssetLoader([this](std::string const& path) {
                return ReadTextFile(ResolveProjectAsset(args_.project_dir, path));
            });
            auto const project = loader.LoadProject(args_.project_dir / "project.xml");
            auto const scene_path = ResolveProjectAsset(args_.project_dir, project.entry_scene);
            auto const scene = loader.LoadScene(scene_path);

            if (reset_gpu_resources) {
                ctx_.Device().waitIdle();
                renderer_.ResetFrameGraphRuntimeState(ctx_);
                renderer_.GetResourceSystem().Clear();
                renderer_.GetMaterialSystem().Clear();
                renderer_.SetVkContext(&ctx_);
                RegisterPreviewFontAtlas();
            }

            float const aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 16.0f / 9.0f;
            scene_world_.RebuildFromScene(scene,
                                          renderer_.GetResourceSystem(),
                                          renderer_.GetMaterialSystem(),
                                          aspect);
            ui_runtime_.SetViewportSize(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
            ui_runtime_.SetInputViewportSize(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 0);
            ui_runtime_.RebuildFromScene(scene);
            script_host_.Bind(scene,
                              &scene_world_,
                              &ui_runtime_,
                              &renderer_.GetResourceSystem(),
                              &renderer_.GetMaterialSystem(),
                              args_.project_dir);
            last_error_.clear();
            std::cout << "[preview] loaded scene: " << scene_path << "\n";
        } catch (std::exception const& exc) {
            last_error_ = exc.what();
            std::cerr << "[preview] reload failed, keeping previous scene: " << last_error_ << "\n";
        }
    }

    void RegisterPreviewFontAtlas()
    {
        uint32_t constexpr atlas_width = 1024;
        uint32_t constexpr atlas_height = 512;
        auto pixels = BuildPreviewFontAtlas(atlas_width, atlas_height);
        renderer_.GetResourceSystem().GetTextureManager().LoadTextureFromData(
            "__ave_font_atlas",
            atlas_width,
            atlas_height,
            pixels.data(),
            1);
    }

    void MainLoop()
    {
        auto last_time = std::chrono::steady_clock::now();
        auto last_watch = std::chrono::steady_clock::now();

        while (!glfwWindowShouldClose(window_)) {
            glfwPollEvents();

            if (resize_pending_) {
                resize_pending_ = false;
                ctx_.Device().waitIdle();
                renderer_.ShutdownFrameGraphBackend();
                swapchain_.Recreate(ctx_);
                sync_.EnsureRenderFinishedSize(ctx_, swapchain_.ImageCount());
                if (!renderer_.InitializeFrameGraphBackend(ctx_, swapchain_, sync_)) {
                    throw std::runtime_error("failed to recreate FrameGraph backend");
                }
                scene_world_.SetAspectRatio(static_cast<float>(width_) / static_cast<float>(height_));
                ui_runtime_.SetViewportSize(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
                ui_runtime_.SetInputViewportSize(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 0);
            }

            auto const now = std::chrono::steady_clock::now();
            float delta = std::chrono::duration<float>(now - last_time).count();
            last_time = now;
            delta = std::min(delta, 0.1f);

            if (now - last_watch > kHotReloadInterval) {
                last_watch = now;
                if (file_stamps_.Refresh(args_.project_dir)) {
                    reload_pending_ = true;
                    last_asset_change_ = now;
                    std::cout << "[preview] project asset changed, waiting for stable write...\n";
                }
            }

            if (reload_pending_ && !resize_pending_ && now - last_asset_change_ >= kHotReloadDebounce) {
                reload_pending_ = false;
                scene_pointer_active_ = false;
                script_host_.DispatchPointerUp();
                frame_data_ = {};
                std::cout << "[preview] reloading scene after asset change...\n";
                bool const java_compiled = CompilePreviewJavaScripts();
                script_host_.SetJavaClassDir(java_compiled ? current_java_class_dir_ : std::filesystem::path{});
                LoadProjectAndScene(false);
                last_time = std::chrono::steady_clock::now();
            }

            frame_data_ = {};
            ui_runtime_.Update(delta);
            script_host_.Update(delta);
            scene_world_.BuildFrameData(frame_index_, frame_data_);
            ui_runtime_.BuildFrameUi(frame_data_.ui_items);

            auto const result = renderer_.RenderFrameGraphFrame(frame_data_, ctx_, swapchain_, sync_, sync_frame_index_);
            if (result == ave::render::FrameGraphRenderResult::SwapchainOutOfDate) {
                resize_pending_ = true;
            }
            ++frame_index_;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void DispatchUiAction(std::optional<ave::ui::UIRuntime::UiAction> const& action)
    {
        if (!action.has_value()) {
            return;
        }
        if (action->type == ave::ui::UIRuntime::ActionType::Click) {
            script_host_.DispatchClick(action->target, action->method);
        } else if (action->type == ave::ui::UIRuntime::ActionType::ValueChanged) {
            script_host_.DispatchValueChanged(action->target, action->method, action->source_id, action->value);
        }
    }

    glm::vec2 UiPointerNdc(double x, double y) const
    {
        double const safe_width = static_cast<double>(std::max(width_, 1));
        double const safe_height = static_cast<double>(std::max(height_, 1));
        // Preview UI vertices are authored in the same top-left convention as the
        // GLFW cursor, so pass NDC explicitly instead of letting UIRuntime flip Y.
        return {
            static_cast<float>((std::clamp(x, 0.0, safe_width) / safe_width) * 2.0 - 1.0),
            static_cast<float>((std::clamp(y, 0.0, safe_height) / safe_height) * 2.0 - 1.0),
        };
    }

    void OnMouseButton(int button, int action)
    {
        if (button != GLFW_MOUSE_BUTTON_LEFT) {
            return;
        }

        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window_, &x, &y);
        glm::vec2 const ui_ndc = UiPointerNdc(x, y);
        if (action == GLFW_PRESS) {
            auto ui_action = ui_runtime_.HandlePointerNdcDown(ui_ndc.x, ui_ndc.y);
            DispatchUiAction(ui_action);
            scene_pointer_active_ = !ui_action.has_value();
            if (scene_pointer_active_) {
                script_host_.DispatchPointerDown(x, y);
            }
        } else if (action == GLFW_RELEASE) {
            auto ui_action = ui_runtime_.HandlePointerNdcUp(ui_ndc.x, ui_ndc.y);
            DispatchUiAction(ui_action);
            if (scene_pointer_active_) {
                script_host_.DispatchPointerUp();
            }
            scene_pointer_active_ = false;
        }
    }

    void OnCursorPos(double x, double y)
    {
        glm::vec2 const ui_ndc = UiPointerNdc(x, y);
        auto ui_action = ui_runtime_.HandlePointerNdcMove(ui_ndc.x, ui_ndc.y);
        DispatchUiAction(ui_action);
        if (scene_pointer_active_ && !ui_action.has_value()) {
            script_host_.DispatchPointerMove(x, y);
        }
    }

    void OnScroll(double y_offset)
    {
        (void)y_offset;
    }

    void ShutdownRuntime()
    {
        if (ctx_.IsInitialized()) {
            try {
                ctx_.Device().waitIdle();
            } catch (...) {
            }
            renderer_.Shutdown();
            swapchain_.Shutdown(ctx_);
            sync_.Shutdown(ctx_);
            renderer_.GetResourceSystem().Clear();
            ctx_.Shutdown();
        }
        jobs_.Stop();
    }

    PreviewArgs args_;
    GLFWwindow* window_ = nullptr;
    int width_ = 1280;
    int height_ = 720;
    bool resize_pending_ = false;
    bool scene_pointer_active_ = false;
    bool reload_pending_ = false;
    std::chrono::steady_clock::time_point last_asset_change_{};
    uint64_t java_compile_generation_ = 0;
    uint64_t frame_index_ = 0;
    uint32_t sync_frame_index_ = 0;
    std::string last_error_;
    std::filesystem::path current_java_class_dir_;

    ave::core::JobSystem jobs_{};
    ave::render::Renderer renderer_{};
    ave::scene::SceneWorld scene_world_{};
    ave::ui::UIRuntime ui_runtime_{};
    PreviewScriptHost script_host_{};
    ave::core::FrameData frame_data_{};
    vkfw::VkContext ctx_{};
    vkfw::VkSwapchain swapchain_{};
    vkfw::VkFrameSync sync_{};
    FileStampCache file_stamps_{};
};

} // namespace

int RunPreview(int argc, char** argv)
{
    try {
        PreviewApp app(ParsePreviewArgs(argc, argv));
        return app.Run();
    } catch (std::exception const& exc) {
        std::cerr << "[preview] " << exc.what() << "\n";
        return 2;
    }
}
