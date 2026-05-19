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
    bool const raster_ready = !model_vertices_.empty()
        ? renderer_.InitializeRasterModel(ctx_, swapchainWrap_, sync_, model_vertices_, shaders)
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
            swapchainWrap_.Shutdown(ctx_);
            sync_.Shutdown(ctx_);
            ctx_.Shutdown();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }
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
    model_vertices_.clear();

    ave::project::XmlSceneLoader loader;
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);

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

            std::vector<ave::resource::ObjMeshVertex> obj_vertices;
            if (!ave::resource::ParseObjMeshText(text, obj_vertices)) {
                __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OBJ asset has no usable geometry: %s", mesh.mesh.c_str());
                return false;
            }
            size_t texcoord_count = 0;
            for (auto const& vertex : obj_vertices) {
                if (vertex.has_texcoord) {
                    ++texcoord_count;
                }
            }
            model_vertices_.insert(model_vertices_.end(), obj_vertices.begin(), obj_vertices.end());
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "Loaded OBJ mesh %s with %zu expanded vertices (%zu with UVs)",
                                mesh.mesh.c_str(),
                                obj_vertices.size(),
                                texcoord_count);
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "Texture asset staged for future sampling: textures/viking_room.png");
            continue;
        }

        for (auto const& source : mesh.vertices) {
            ave::render::RasterColorVertex vertex{};
            vertex.position = source.position;
            vertex.color = source.color;
            vertices_.push_back(vertex);
        }
    }

    if (vertices_.size() < 3 && model_vertices_.size() < 3) {
        logError("Scene XML must define at least three <Vertex> entries or a valid external mesh.");
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO,
                        kLogTag,
                        "Loaded scene mesh data from %s (%zu inline preview vertices, %zu model vertices)",
                        project.entry_scene.c_str(),
                        vertices_.size(),
                        model_vertices_.size());
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
