#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"
#include "ave/resource/ResourceSystem.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>


namespace ave::android {

namespace {

constexpr uint32_t kFramesInFlight = 2;

constexpr char kLogTag[] = "AveRuntime";

void logInfo(char const* message)
{
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
}

void logError(char const* message)
{
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

std::array<float, 3> TransformPreviewPosition(std::array<float, 3> const& position)
{
    return {
        position[0],
        position[2],
        -position[1],
    };
}

void PreparePreviewMeshData(ave::project::MeshData& mesh)
{
    if (mesh.vertices.empty()) {
        return;
    }

    std::vector<std::array<float, 3>> positions;
    positions.reserve(mesh.vertices.size());
    for (auto const& vertex : mesh.vertices) {
        positions.push_back(TransformPreviewPosition(vertex.position));
    }

    auto min_pos = positions.front();
    auto max_pos = positions.front();
    for (auto const& position : positions) {
        for (int i = 0; i < 3; ++i) {
            min_pos[i] = std::min(min_pos[i], position[i]);
            max_pos[i] = std::max(max_pos[i], position[i]);
        }
    }

    std::array<float, 3> const center{
        (min_pos[0] + max_pos[0]) * 0.5f,
        (min_pos[1] + max_pos[1]) * 0.5f,
        (min_pos[2] + max_pos[2]) * 0.5f,
    };
    float const extent_x = max_pos[0] - min_pos[0];
    float const extent_y = max_pos[1] - min_pos[1];
    float const extent_z = max_pos[2] - min_pos[2];
    float const max_extent = std::max({extent_x, extent_y, extent_z, 0.0001f});
    float const scale = 1.6f / max_extent;
    bool const has_any_uv = std::any_of(
        mesh.vertices.begin(),
        mesh.vertices.end(),
        [](ave::project::VertexData const& vertex) {
            return vertex.texcoord0 != std::array<float, 2>{0.0f, 0.0f};
        });

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& vertex = mesh.vertices[i];
        auto const& position = positions[i];
        std::array<float, 4> color{0.85f, 0.82f, 0.78f, 1.0f};
        if (has_any_uv) {
            auto const& uv = vertex.texcoord0;
            color = {
                std::clamp(uv[0], 0.0f, 1.0f),
                std::clamp(uv[1], 0.0f, 1.0f),
                std::clamp(1.0f - uv[0], 0.0f, 1.0f),
                1.0f,
            };
        }

        vertex.position = {
            (position[0] - center[0]) * scale,
            (position[1] - center[1]) * scale,
            (position[2] - center[2]) * scale,
        };
        vertex.color = color;
    }

    if (mesh.indices.empty()) {
        mesh.indices.resize(mesh.vertices.size());
        for (uint32_t i = 0; i < mesh.indices.size(); ++i) {
            mesh.indices[i] = i;
        }
    }
}

} // namespace

bool MinimalVulkanTriangle::create(AAssetManager* assets, std::string project_path)
{
    assets_ = assets;
    project_path_ = std::move(project_path);
    logProjectAsset();
    return true;
}

void MinimalVulkanTriangle::destroy()
{
    clearSurface();
    logInfo("Ave runtime destroyed.");
}

void MinimalVulkanTriangle::setSurface(ANativeWindow* window)
{
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

    sync_.Init(ctx_, kFramesInFlight);

    vkfw::SwapchainInfo si{};
    swapchainWrap_.Init(ctx_, si);
    sync_.EnsureRenderFinishedSize(ctx_, swapchainWrap_.ImageCount());

    if (!loadSceneMesh()) {
        logError("Failed to load scene mesh.");
        return;
    }

    ave::render::RasterShaderCode shaders{
        readShaderAsset("compiled_shaders/solid_triangle.vert.spv"),
        readShaderAsset("compiled_shaders/solid_triangle.frag.spv"),
    };
    bool const raster_ready = model_mesh_id_ != 0
        ? renderer_.InitializeRasterMeshResource(ctx_, swapchainWrap_, sync_, model_mesh_id_, shaders)
        : renderer_.InitializeRaster(ctx_, swapchainWrap_, sync_, vertices_, shaders);
    if (!raster_ready) {
        logError("Failed to initialize Vulkan triangle renderer.");
        return;
    }

    drawFrame();
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
}

void MinimalVulkanTriangle::resize(int width, int height)
{
    width_ = width;
    height_ = height;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Surface resized: %dx%d", width_, height_);
    if (ctx_.IsInitialized() && window_ != nullptr) {
        drawFrame();
    }
}

bool MinimalVulkanTriangle::loadSceneMesh()
{
    vertices_.clear();
    model_mesh_id_ = 0;

    ave::project::XmlSceneLoader loader;
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);
    auto& mesh_manager = renderer_.GetResourceSystem().GetMeshManager();

    for (auto const& object : scene.objects) {
        if (!object.components.mesh_renderer.has_value()) {
            continue;
        }

        auto const& mesh = *object.components.mesh_renderer;
        if (!mesh.mesh.empty()) {
            auto const text = readTextAsset(mesh.mesh.c_str());
            if (text.empty()) {
                __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OBJ asset not found or empty: %s", mesh.mesh.c_str());
                return false;
            }

            ave::project::MeshData obj_mesh{};
            obj_mesh.id = mesh.mesh;
            obj_mesh.source = mesh.mesh;
            if (!mesh_manager.ParseObjMeshText(text, obj_mesh)) {
                __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OBJ asset has no usable geometry: %s", mesh.mesh.c_str());
                return false;
            }

            size_t texcoord_count = 0;
            for (auto const& vertex : obj_mesh.vertices) {
                if (vertex.texcoord0 != std::array<float, 2>{0.0f, 0.0f}) {
                    ++texcoord_count;
                }
            }
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "Loaded OBJ mesh %s with %zu unique vertices and %zu indices (%zu with UVs)",
                                mesh.mesh.c_str(),
                                obj_mesh.vertices.size(),
                                obj_mesh.indices.size(),
                                texcoord_count);
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "Texture asset staged for future sampling: textures/viking_room.png");

            PreparePreviewMeshData(obj_mesh);
            model_mesh_id_ = mesh_manager.LoadMeshFromData(mesh.mesh, obj_mesh);
            if (model_mesh_id_ == 0) {
                __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Failed to create preview mesh buffers for %s", mesh.mesh.c_str());
                return false;
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
        logError("Scene XML must define at least three <Vertex> entries or a valid external mesh.");
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO,
                        kLogTag,
                        "Loaded scene mesh data from %s (%zu inline preview vertices, model mesh id %u)",
                        project.entry_scene.c_str(),
                        vertices_.size(),
                        model_mesh_id_);
    return true;
}

void MinimalVulkanTriangle::drawFrame()
{
    renderer_.RenderRasterFrame(ctx_, swapchainWrap_, sync_, frame_index_);
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
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded project asset: %s (%ld bytes)", project_path_.c_str(), static_cast<long>(AAsset_getLength(project)));
        AAsset_close(project);
    } else {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Project asset not found: %s", project_path_.c_str());
    }
}

std::vector<uint32_t> MinimalVulkanTriangle::readShaderAsset(char const* path) const
{
    if (assets_ == nullptr) {
        return {};
    }

    AAsset* asset = AAssetManager_open(assets_, path, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Shader asset not found: %s", path);
        return {};
    }

    size_t const size = static_cast<size_t>(AAsset_getLength(asset));
    std::vector<uint32_t> words((size + sizeof(uint32_t) - 1) / sizeof(uint32_t));
    int const read = AAsset_read(asset, words.data(), size);
    AAsset_close(asset);

    if (read < 0 || static_cast<size_t>(read) != size || size % sizeof(uint32_t) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Invalid SPIR-V asset: %s", path);
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
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Text asset not found: %s", path);
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

} // namespace ave::android
