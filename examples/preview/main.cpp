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
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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

constexpr uint32_t kFramesInFlight = 2;
constexpr float kPi = 3.14159265358979323846f;
constexpr auto kHotReloadInterval = std::chrono::milliseconds(500);
constexpr auto kHotReloadDebounce = std::chrono::milliseconds(300);

struct PreviewArgs {
    std::filesystem::path project_dir;
    std::filesystem::path compiled_shader_dir;
    uint32_t width = 1280;
    uint32_t height = 720;
};

std::filesystem::path Normalize(std::filesystem::path path)
{
    return path.lexically_normal();
}

std::string ReadTextFile(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to read text asset: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> ReadBinaryFile(std::filesystem::path const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path ResolveProjectAsset(std::filesystem::path const& project_dir, std::string const& asset_path)
{
    std::filesystem::path path(asset_path);
    if (path.is_absolute()) {
        return Normalize(path);
    }
    return Normalize(project_dir / path);
}

std::vector<uint32_t> ReadSpirv(std::filesystem::path const& path)
{
    auto bytes = ReadBinaryFile(path);
    if (bytes.empty() || bytes.size() % sizeof(uint32_t) != 0) {
        return {};
    }
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

std::vector<uint8_t> BuildPreviewFontAtlas(uint32_t width, uint32_t height)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u, 0u);
    uint32_t const cell_w = width / 16u;
    uint32_t const cell_h = height / 8u;

#if defined(_WIN32)
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width);
    bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(height);
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* dib_pixels = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateDIBSection(memory_dc, &bitmap_info, DIB_RGB_COLORS, &dib_pixels, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);

    RECT full_rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    HBRUSH black_brush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FillRect(memory_dc, &full_rect, black_brush);

    int const font_height = -static_cast<int>(std::max(8u, cell_h - 10u));
    HFONT font = CreateFontA(font_height,
                             0,
                             0,
                             0,
                             FW_NORMAL,
                             FALSE,
                             FALSE,
                             FALSE,
                             ANSI_CHARSET,
                             OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             FF_DONTCARE,
                             "Consolas");
    HGDIOBJ old_font = SelectObject(memory_dc, font);
    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, RGB(255, 255, 255));

    for (uint32_t glyph = 0; glyph < 128u; ++glyph) {
        uint32_t const col = glyph % 16u;
        uint32_t const row = glyph / 16u;
        RECT cell_rect{
            static_cast<LONG>(col * cell_w + 4u),
            static_cast<LONG>(row * cell_h + 4u),
            static_cast<LONG>((col + 1u) * cell_w - 4u),
            static_cast<LONG>((row + 1u) * cell_h - 4u),
        };
        char const ch = glyph >= 32u ? static_cast<char>(glyph) : ' ';
        DrawTextA(memory_dc, &ch, 1, &cell_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    auto const* bgra = static_cast<uint8_t const*>(dib_pixels);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t const idx = (static_cast<size_t>(y) * width + x) * 4u;
            uint8_t const b = bgra[idx + 0];
            uint8_t const g = bgra[idx + 1];
            uint8_t const r = bgra[idx + 2];
            uint8_t const a = std::max({r, g, b});
            pixels[idx + 0] = 255u;
            pixels[idx + 1] = 255u;
            pixels[idx + 2] = 255u;
            pixels[idx + 3] = a;
        }
    }

    SelectObject(memory_dc, old_font);
    DeleteObject(font);
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
#else
    for (uint32_t glyph = 0; glyph < 128u; ++glyph) {
        uint32_t const col = glyph % 16u;
        uint32_t const row = glyph / 16u;
        uint32_t const x0 = col * cell_w;
        uint32_t const y0 = row * cell_h;
        for (uint32_t y = cell_h / 3u; y < cell_h * 2u / 3u; ++y) {
            for (uint32_t x = cell_w / 3u; x < cell_w * 2u / 3u; ++x) {
                size_t const idx = (static_cast<size_t>(y0 + y) * width + (x0 + x)) * 4u;
                pixels[idx + 0] = 255u;
                pixels[idx + 1] = 255u;
                pixels[idx + 2] = 255u;
                pixels[idx + 3] = 255u;
            }
        }
    }
#endif

    return pixels;
}

class FileStampCache {
public:
    bool Refresh(std::filesystem::path const& root)
    {
        std::unordered_map<std::string, std::filesystem::file_time_type> next;
        if (!std::filesystem::exists(root)) {
            return false;
        }

        for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto const path = entry.path();
            auto const ext = path.extension().string();
            if (ext != ".xml" && ext != ".spv" && ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
                ext != ".obj" && ext != ".vert" && ext != ".frag" && ext != ".comp" && ext != ".java") {
                continue;
            }
            next[path.generic_string()] = entry.last_write_time();
        }

        bool const changed = !initialized_ || next != stamps_;
        stamps_ = std::move(next);
        initialized_ = true;
        return changed;
    }

private:
    bool initialized_ = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> stamps_;
};

class PreviewScriptHost {
private:
    enum class ScriptType {
        Unknown,
        CameraController,
        HierarchyDemo,
        LightControl,
        RenderBackendDebugText,
        MySpawner,
        SelfDestruct,
    };

    enum class CameraMode {
        Translate,
        Rotate,
    };

    struct PreviewScript {
        ScriptType type = ScriptType::Unknown;
        std::string class_name;
        std::string simple_name;
        std::string object_id;
        std::unordered_map<std::string, std::string> params;

        CameraMode camera_mode = CameraMode::Translate;
        std::string mode_button_id = "switch_mode_button";
        std::string translate_texture = "textures/tanslate.png";
        std::string rotation_texture = "textures/rotation.png";
        float translate_sensitivity = 0.0015f;
        float rotate_sensitivity = 0.15f;
        float zoom_sensitivity = 0.02f;
        float min_pitch = -80.0f;
        float max_pitch = 80.0f;
        float min_distance = 1.0f;
        float max_distance = 30.0f;
        glm::vec3 target{0.0f};
        float distance = 8.0f;
        float pitch = 0.0f;
        float yaw = 0.0f;
        bool dragging = false;
        float last_x = 0.0f;
        float last_y = 0.0f;

        float angle = 0.0f;
        float speed = 35.0f;

        std::string light_id = "KeyLight";
        std::string auto_button_id = "light_auto_button";
        std::string x_slider_id = "light_x_slider";
        std::string y_slider_id = "light_y_slider";
        std::string z_slider_id = "light_z_slider";
        bool auto_rotate = true;
        float sun_time = 1.57f;
        float rotate_speed = 0.4f;
        float radius = 8.0f;
        glm::vec3 light_position{0.0f, 6.0f, 6.0f};
        glm::vec3 min_light{-8.0f, 0.5f, -8.0f};
        glm::vec3 max_light{8.0f, 10.0f, 8.0f};

        float refresh_timer = 0.0f;

        int spawn_count = 0;
        std::string prefab_path = "prefabs/my_sphere.prefab.xml";
        float spawn_scale = 2.0f;
        std::vector<glm::vec3> spawn_positions;

        float timer = 0.0f;
        float life_time = 3.0f;
    };

public:
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
        scripts_.clear();
        known_objects_.clear();
        AddScriptsFromScene(scene);
    }

    void Update(float delta_time)
    {
        if (scene_world_ == nullptr || ui_runtime_ == nullptr) {
            return;
        }

        std::vector<std::string> destroy_queue;
        for (auto& script : scripts_) {
            switch (script.type) {
            case ScriptType::HierarchyDemo:
                script.angle += script.speed * delta_time;
                scene_world_->SetObjectRotation(script.object_id, {0.0f, script.angle, 0.0f});
                break;
            case ScriptType::LightControl:
                if (script.auto_rotate) {
                    script.sun_time += delta_time * script.rotate_speed;
                    float const angle = script.sun_time;
                    script.light_position.x = std::cos(angle) * script.radius;
                    script.light_position.y = std::abs(std::sin(angle) * script.radius) + 1.5f;
                    script.light_position.z = std::sin(angle * 0.5f) * (script.radius * 0.5f);
                    ApplyLight(script);
                    UpdateLightUi(script);
                }
                break;
            case ScriptType::RenderBackendDebugText:
                script.refresh_timer -= delta_time;
                if (script.refresh_timer <= 0.0f) {
                    script.refresh_timer = 0.25f;
                    ui_runtime_->SetObjectText(script.object_id, "DYNAMIC RENDERING");
                }
                break;
            case ScriptType::SelfDestruct:
                script.timer += delta_time;
                if (script.timer >= script.life_time) {
                    destroy_queue.push_back(script.object_id);
                }
                break;
            default:
                break;
            }
        }

        for (auto const& object_id : destroy_queue) {
            auto destroyed = scene_world_->DestroyObject(object_id, *resources_, *materials_);
            for (auto const& id : destroyed) {
                known_objects_.erase(id);
            }
            scripts_.erase(std::remove_if(scripts_.begin(), scripts_.end(), [&](PreviewScript const& script) {
                return std::find(destroyed.begin(), destroyed.end(), script.object_id) != destroyed.end();
            }), scripts_.end());
        }

        AddScriptsFromScene(scene_world_->GetSceneData());
    }

    void DispatchClick(std::string const& target, std::string const& method)
    {
        for (auto& script : scripts_) {
            if (!MatchesTarget(script, target)) {
                continue;
            }
            if (script.type == ScriptType::CameraController && (method == "switchMode" || method == "interact")) {
                script.camera_mode = script.camera_mode == CameraMode::Translate ? CameraMode::Rotate : CameraMode::Translate;
                ApplyCameraModeTexture(script);
                return;
            }
            if (script.type == ScriptType::LightControl && (method == "toggleAuto" || method == "light_auto_button")) {
                script.auto_rotate = !script.auto_rotate;
                UpdateLightButton(script);
                return;
            }
            if (script.type == ScriptType::MySpawner && method == "spawnDynamicObject") {
                SpawnDynamicObject(script);
                return;
            }
        }
    }

    void DispatchValueChanged(std::string const& target, std::string const& source_id, float value)
    {
        for (auto& script : scripts_) {
            if (!MatchesTarget(script, target) || script.type != ScriptType::LightControl) {
                continue;
            }
            if (script.auto_rotate) {
                script.auto_rotate = false;
                UpdateLightButton(script);
            }
            if (source_id == script.x_slider_id) {
                script.light_position.x = value;
            } else if (source_id == script.y_slider_id) {
                script.light_position.y = value;
            } else if (source_id == script.z_slider_id) {
                script.light_position.z = value;
            } else {
                return;
            }
            ApplyLight(script);
            UpdateLightUi(script);
            return;
        }
    }

    void BeginCameraDrag(double x, double y)
    {
        if (auto* camera = FindFirst(ScriptType::CameraController)) {
            camera->dragging = true;
            camera->last_x = static_cast<float>(x);
            camera->last_y = static_cast<float>(y);
        }
    }

    void UpdateCameraDrag(double x, double y)
    {
        auto* camera = FindFirst(ScriptType::CameraController);
        if (camera == nullptr || !camera->dragging) {
            return;
        }

        float const raw_dx = static_cast<float>(x) - camera->last_x;
        float const raw_dy = static_cast<float>(y) - camera->last_y;
        camera->last_x = static_cast<float>(x);
        camera->last_y = static_cast<float>(y);
        float const dx = raw_dx;
        float const dy = raw_dy;

        if (camera->camera_mode == CameraMode::Translate) {
            glm::vec3 const forward = CameraForward(*camera);
            glm::vec3 right = glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f});
            if (glm::dot(right, right) <= 0.0001f) {
                right = {1.0f, 0.0f, 0.0f};
            } else {
                right = glm::normalize(right);
            }
            glm::vec3 const up = glm::normalize(glm::cross(right, forward));
            float const scale = camera->translate_sensitivity * camera->distance;
            camera->target += (-right * dx + up * dy) * scale;
        } else {
            camera->yaw -= dx * camera->rotate_sensitivity;
            camera->pitch = std::clamp(camera->pitch - dy * camera->rotate_sensitivity,
                                       camera->min_pitch,
                                       camera->max_pitch);
        }
        ApplyCameraTransform(*camera);
    }

    void EndCameraDrag()
    {
        if (auto* camera = FindFirst(ScriptType::CameraController)) {
            camera->dragging = false;
        }
    }

    void ZoomCamera(double delta)
    {
        if (auto* camera = FindFirst(ScriptType::CameraController)) {
            camera->distance = std::clamp(camera->distance - static_cast<float>(delta) * 60.0f * camera->zoom_sensitivity,
                                          camera->min_distance,
                                          camera->max_distance);
            ApplyCameraTransform(*camera);
        }
    }

private:
    static std::string SimpleName(std::string const& class_name)
    {
        auto const pos = class_name.find_last_of('.');
        return pos == std::string::npos ? class_name : class_name.substr(pos + 1);
    }

    static ScriptType TypeFromClass(std::string const& simple_name)
    {
        if (simple_name == "CameraController") return ScriptType::CameraController;
        if (simple_name == "HierarchyDemoController") return ScriptType::HierarchyDemo;
        if (simple_name == "LightControlScript") return ScriptType::LightControl;
        if (simple_name == "RenderBackendDebugText") return ScriptType::RenderBackendDebugText;
        if (simple_name == "MySpawner") return ScriptType::MySpawner;
        if (simple_name == "SelfDestruct") return ScriptType::SelfDestruct;
        return ScriptType::Unknown;
    }

    static float ReadFloat(std::unordered_map<std::string, std::string> const& params,
                           std::string const& key,
                           float fallback)
    {
        auto const it = params.find(key);
        if (it == params.end()) {
            return fallback;
        }
        try {
            return std::stof(it->second);
        } catch (...) {
            return fallback;
        }
    }

    static std::string ReadString(std::unordered_map<std::string, std::string> const& params,
                                  std::string const& key,
                                  std::string fallback)
    {
        auto const it = params.find(key);
        return it == params.end() || it->second.empty() ? std::move(fallback) : it->second;
    }

    static bool ReadBool(std::unordered_map<std::string, std::string> const& params,
                         std::string const& key,
                         bool fallback)
    {
        auto const it = params.find(key);
        if (it == params.end()) {
            return fallback;
        }
        return it->second == "1" || it->second == "true" || it->second == "TRUE" || it->second == "on";
    }

    static glm::vec3 CameraForward(PreviewScript const& script)
    {
        float const yaw_rad = script.yaw * kPi / 180.0f;
        float const pitch_rad = script.pitch * kPi / 180.0f;
        float const cos_pitch = std::cos(pitch_rad);
        glm::vec3 forward{
            -std::sin(yaw_rad) * cos_pitch,
            std::sin(pitch_rad),
            -std::cos(yaw_rad) * cos_pitch,
        };
        if (glm::dot(forward, forward) <= 0.0001f) {
            return {0.0f, 0.0f, -1.0f};
        }
        return glm::normalize(forward);
    }

    bool MatchesTarget(PreviewScript const& script, std::string const& target) const
    {
        return target == script.object_id || target == script.simple_name || target == script.class_name;
    }

    void AddScriptsFromScene(ave::project::SceneData const& scene)
    {
        for (auto const& object : scene.objects) {
            if (!object.components.script.has_value() || known_objects_.find(object.id) != known_objects_.end()) {
                continue;
            }

            auto const& binding = *object.components.script;
            PreviewScript script{};
            script.class_name = binding.java_class;
            script.simple_name = SimpleName(binding.java_class);
            script.type = TypeFromClass(script.simple_name);
            script.object_id = object.id;
            script.params = binding.parameters;
            StartScript(script);
            known_objects_[object.id] = true;
            scripts_.push_back(std::move(script));
        }
    }

    void StartScript(PreviewScript& script)
    {
        switch (script.type) {
        case ScriptType::CameraController:
            StartCamera(script);
            break;
        case ScriptType::HierarchyDemo:
            script.speed = ReadFloat(script.params, "speed", script.speed);
            break;
        case ScriptType::LightControl:
            StartLight(script);
            break;
        case ScriptType::RenderBackendDebugText:
            script.refresh_timer = 0.0f;
            if (ui_runtime_ != nullptr) {
                ui_runtime_->SetObjectText(script.object_id, "DYNAMIC RENDERING");
            }
            break;
        case ScriptType::MySpawner:
            script.prefab_path = ReadString(script.params, "prefab", script.prefab_path);
            script.spawn_scale = ReadFloat(script.params, "spawnScale", script.spawn_scale);
            ParseSpawnPositions(script);
            break;
        case ScriptType::SelfDestruct:
            script.life_time = ReadFloat(script.params, "lifetime", script.life_time);
            break;
        default:
            std::cout << "[preview] script mock not implemented: " << script.class_name << "\n";
            break;
        }
    }

    void StartCamera(PreviewScript& script)
    {
        script.mode_button_id = ReadString(script.params, "modeButton", script.mode_button_id);
        script.translate_texture = ReadString(script.params, "translateTexture", script.translate_texture);
        script.rotation_texture = ReadString(script.params, "rotationTexture", script.rotation_texture);
        script.translate_sensitivity = ReadFloat(script.params, "translateSensitivity", script.translate_sensitivity);
        script.rotate_sensitivity = ReadFloat(script.params, "rotateSensitivity", script.rotate_sensitivity);
        script.zoom_sensitivity = ReadFloat(script.params, "zoomSensitivity", script.zoom_sensitivity);
        script.min_pitch = ReadFloat(script.params, "minPitch", script.min_pitch);
        script.max_pitch = ReadFloat(script.params, "maxPitch", script.max_pitch);
        script.min_distance = ReadFloat(script.params, "minDistance", ReadFloat(script.params, "minZ", script.min_distance));
        script.max_distance = ReadFloat(script.params, "maxDistance", ReadFloat(script.params, "maxZ", script.max_distance));
        script.target.x = ReadFloat(script.params, "targetX", script.target.x);
        script.target.y = ReadFloat(script.params, "targetY", script.target.y);
        script.target.z = ReadFloat(script.params, "targetZ", script.target.z);

        glm::vec3 position{};
        if (scene_world_ != nullptr && scene_world_->GetObjectPosition(script.object_id, position)) {
            position.x = ReadFloat(script.params, "x", position.x);
            position.y = ReadFloat(script.params, "y", position.y);
            position.z = ReadFloat(script.params, "z", position.z);
            glm::vec3 const offset = position - script.target;
            script.distance = std::clamp(glm::length(offset), script.min_distance, script.max_distance);
            if (script.distance > 0.0001f) {
                glm::vec3 const forward = -offset / script.distance;
                script.pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * 180.0f / kPi;
                script.yaw = std::atan2(-forward.x, -forward.z) * 180.0f / kPi;
            }
        }

        script.pitch = ReadFloat(script.params, "pitch", script.pitch);
        script.yaw = ReadFloat(script.params, "yaw", script.yaw);
        ApplyCameraTransform(script);
        ApplyCameraModeTexture(script);
    }

    void StartLight(PreviewScript& script)
    {
        script.light_id = ReadString(script.params, "light", script.light_id);
        script.auto_button_id = ReadString(script.params, "autoButton", script.auto_button_id);
        script.x_slider_id = ReadString(script.params, "xSlider", script.x_slider_id);
        script.y_slider_id = ReadString(script.params, "ySlider", script.y_slider_id);
        script.z_slider_id = ReadString(script.params, "zSlider", script.z_slider_id);
        script.auto_rotate = ReadBool(script.params, "auto", script.auto_rotate);
        script.rotate_speed = ReadFloat(script.params, "speed", script.rotate_speed);
        script.radius = ReadFloat(script.params, "radius", script.radius);
        script.min_light.x = ReadFloat(script.params, "minX", script.min_light.x);
        script.max_light.x = ReadFloat(script.params, "maxX", script.max_light.x);
        script.min_light.y = ReadFloat(script.params, "minY", script.min_light.y);
        script.max_light.y = ReadFloat(script.params, "maxY", script.max_light.y);
        script.min_light.z = ReadFloat(script.params, "minZ", script.min_light.z);
        script.max_light.z = ReadFloat(script.params, "maxZ", script.max_light.z);

        glm::vec3 light_position{};
        if (scene_world_ != nullptr && scene_world_->GetObjectPosition(script.light_id, light_position)) {
            script.light_position = light_position;
        }
        script.light_position.x = ReadFloat(script.params, "x", script.light_position.x);
        script.light_position.y = ReadFloat(script.params, "y", script.light_position.y);
        script.light_position.z = ReadFloat(script.params, "z", script.light_position.z);
        ApplyLight(script);
        UpdateLightUi(script);
        UpdateLightButton(script);
    }

    void ParseSpawnPositions(PreviewScript& script)
    {
        script.spawn_positions.clear();
        auto const it = script.params.find("spawnPositions");
        if (it != script.params.end() && !it->second.empty()) {
            size_t start = 0;
            while (start < it->second.size()) {
                size_t const end = it->second.find(';', start);
                std::string entry = it->second.substr(start, end == std::string::npos ? std::string::npos : end - start);
                std::array<float, 3> values{0.0f, 0.0f, 0.0f};
                size_t part_start = 0;
                for (size_t i = 0; i < values.size(); ++i) {
                    size_t const part_end = entry.find(',', part_start);
                    try {
                        values[i] = std::stof(entry.substr(part_start, part_end == std::string::npos ? std::string::npos : part_end - part_start));
                    } catch (...) {
                        values[i] = 0.0f;
                    }
                    if (part_end == std::string::npos) {
                        break;
                    }
                    part_start = part_end + 1;
                }
                script.spawn_positions.push_back({values[0], values[1], values[2]});
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
        }
        if (script.spawn_positions.empty()) {
            script.spawn_positions = {
                {-3.0f, 3.0f, 0.0f},
                {-1.5f, 3.0f, 0.0f},
                {0.0f, 3.0f, 0.0f},
                {1.5f, 3.0f, 0.0f},
                {3.0f, 3.0f, 0.0f},
            };
        }
    }

    void ApplyCameraTransform(PreviewScript const& script)
    {
        if (scene_world_ == nullptr) {
            return;
        }
        glm::vec3 const forward = CameraForward(script);
        scene_world_->SetObjectPosition(script.object_id, script.target - forward * script.distance);
        scene_world_->SetObjectRotation(script.object_id, {script.pitch, script.yaw, 0.0f});
    }

    void ApplyCameraModeTexture(PreviewScript const& script)
    {
        if (ui_runtime_ != nullptr) {
            ui_runtime_->SetObjectTexture(script.mode_button_id,
                                          script.camera_mode == CameraMode::Translate
                                              ? script.translate_texture
                                              : script.rotation_texture);
        }
    }

    void ApplyLight(PreviewScript const& script)
    {
        if (scene_world_ != nullptr) {
            scene_world_->SetObjectPosition(script.light_id, script.light_position);
        }
    }

    void UpdateLightUi(PreviewScript const& script)
    {
        if (ui_runtime_ == nullptr) {
            return;
        }
        ui_runtime_->SetObjectProgress(script.x_slider_id,
                                       std::clamp(script.light_position.x, script.min_light.x, script.max_light.x));
        ui_runtime_->SetObjectProgress(script.y_slider_id,
                                       std::clamp(script.light_position.y, script.min_light.y, script.max_light.y));
        ui_runtime_->SetObjectProgress(script.z_slider_id,
                                       std::clamp(script.light_position.z, script.min_light.z, script.max_light.z));
    }

    void UpdateLightButton(PreviewScript const& script)
    {
        if (ui_runtime_ != nullptr) {
            ui_runtime_->SetObjectColor(script.auto_button_id,
                                        script.auto_rotate
                                            ? glm::vec4{0.18f, 0.62f, 1.0f, 0.90f}
                                            : glm::vec4{1.0f, 0.55f, 0.18f, 0.90f});
        }
    }

    void SpawnDynamicObject(PreviewScript& script)
    {
        if (scene_world_ == nullptr || resources_ == nullptr || materials_ == nullptr || script.spawn_positions.empty()) {
            return;
        }

        ave::project::XmlSceneLoader loader;
        loader.SetTextAssetLoader([this](std::string const& path) {
            return ReadTextFile(ResolveProjectAsset(project_dir_, path));
        });
        auto const prefab_text = ReadTextFile(ResolveProjectAsset(project_dir_, script.prefab_path));
        auto const prefab = loader.LoadPrefabText(prefab_text);

        ++script.spawn_count;
        int const cycle = (script.spawn_count - 1) / static_cast<int>(script.spawn_positions.size());
        glm::vec3 spawn = script.spawn_positions[static_cast<size_t>((script.spawn_count - 1) % script.spawn_positions.size())];
        spawn.y += static_cast<float>(cycle) * 1.2f;

        std::string const new_id = scene_world_->InstantiatePrefab(prefab, "", *resources_, *materials_);
        if (!new_id.empty()) {
            scene_world_->SetObjectPosition(new_id, spawn);
            scene_world_->SetObjectScale(new_id, {script.spawn_scale, script.spawn_scale, script.spawn_scale});
            AddScriptsFromScene(scene_world_->GetSceneData());
        }
    }

    PreviewScript* FindFirst(ScriptType type)
    {
        auto it = std::find_if(scripts_.begin(), scripts_.end(), [type](PreviewScript const& script) {
            return script.type == type;
        });
        return it == scripts_.end() ? nullptr : &*it;
    }

    ave::scene::SceneWorld* scene_world_ = nullptr;
    ave::ui::UIRuntime* ui_runtime_ = nullptr;
    ave::resource::ResourceSystem* resources_ = nullptr;
    ave::render::MaterialSystem* materials_ = nullptr;
    std::filesystem::path project_dir_;
    std::vector<PreviewScript> scripts_;
    std::unordered_map<std::string, bool> known_objects_;
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
    void InitializeRuntime()
    {
        jobs_.Start(0);

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
            return ReadBinaryFile(ResolveProjectAsset(args_.project_dir, path));
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
                camera_drag_active_ = false;
                script_host_.EndCameraDrag();
                frame_data_ = {};
                std::cout << "[preview] reloading scene after asset change...\n";
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
            script_host_.DispatchValueChanged(action->target, action->source_id, action->value);
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
            camera_drag_active_ = !ui_action.has_value();
            if (camera_drag_active_) {
                script_host_.BeginCameraDrag(x, y);
            }
        } else if (action == GLFW_RELEASE) {
            auto ui_action = ui_runtime_.HandlePointerNdcUp(ui_ndc.x, ui_ndc.y);
            DispatchUiAction(ui_action);
            if (camera_drag_active_) {
                script_host_.EndCameraDrag();
            }
            camera_drag_active_ = false;
        }
    }

    void OnCursorPos(double x, double y)
    {
        glm::vec2 const ui_ndc = UiPointerNdc(x, y);
        auto ui_action = ui_runtime_.HandlePointerNdcMove(ui_ndc.x, ui_ndc.y);
        DispatchUiAction(ui_action);
        if (camera_drag_active_ && !ui_action.has_value()) {
            script_host_.UpdateCameraDrag(x, y);
        }
    }

    void OnScroll(double y_offset)
    {
        script_host_.ZoomCamera(y_offset);
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
    bool camera_drag_active_ = false;
    bool reload_pending_ = false;
    std::chrono::steady_clock::time_point last_asset_change_{};
    uint64_t frame_index_ = 0;
    uint32_t sync_frame_index_ = 0;
    std::string last_error_;

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

PreviewArgs ParseArgs(int argc, char** argv)
{
    if (argc < 2) {
        throw std::runtime_error("usage: ave_preview <project-dir> [compiled-shader-dir] [width] [height]");
    }

    PreviewArgs args{};
    args.project_dir = std::filesystem::absolute(argv[1]).lexically_normal();
    if (argc >= 3) {
        args.compiled_shader_dir = std::filesystem::absolute(argv[2]).lexically_normal();
    }
    if (argc >= 4) {
        args.width = static_cast<uint32_t>(std::max(std::stoi(argv[3]), 1));
    }
    if (argc >= 5) {
        args.height = static_cast<uint32_t>(std::max(std::stoi(argv[4]), 1));
    }
    return args;
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && !defined(NDEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    try {
        PreviewApp app(ParseArgs(argc, argv));
        return app.Run();
    } catch (std::exception const& exc) {
        std::cerr << "[preview] " << exc.what() << "\n";
        return 2;
    }
}
