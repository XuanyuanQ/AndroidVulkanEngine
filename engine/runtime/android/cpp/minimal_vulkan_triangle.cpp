#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"

#include <android/log.h>
#include <algorithm>
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

    ave::rhi::RasterShaderCode shaders{
        readShaderAsset("compiled_shaders/solid_triangle.vert.spv"),
        readShaderAsset("compiled_shaders/solid_triangle.frag.spv"),
    };
    if (!renderer_.Initialize(ctx_, swapchainWrap_, sync_, vertices_, shaders)) {
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

    ave::project::XmlSceneLoader loader;
    auto const project_text = readTextAsset(project_path_.c_str());
    auto const project = loader.LoadProjectText(project_text);
    auto const scene_text = readTextAsset(project.entry_scene.c_str());
    auto const scene = loader.LoadSceneText(scene_text);

    for (auto const& object : scene.objects) {
        if (!object.has_mesh) {
            continue;
        }

        for (auto const& source : object.mesh.vertices) {
            ave::rhi::RasterColorVertex vertex{};
            vertex.position = source.position;
            vertex.color = source.color;
            vertices_.push_back(vertex);
        }
    }

    if (vertices_.size() < 3) {
        logError("Scene XML must define at least three <Vertex> entries.");
        return false;
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Loaded %zu XML-defined vertices from %s", vertices_.size(), project.entry_scene.c_str());
    return true;
}

void MinimalVulkanTriangle::drawFrame()
{
    renderer_.RenderFrame(ctx_, swapchainWrap_, sync_, frame_index_);
}

void MinimalVulkanTriangle::cleanupSurfaceResources()
{
    renderer_.Shutdown();
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
