#define VK_USE_PLATFORM_ANDROID_KHR

#include "minimal_vulkan_triangle.h"

#include "ave/project/XmlSceneLoader.h"

#include <android/log.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
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

bool check(VkResult result, char const* message)
{
    if (result != VK_SUCCESS) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s: VkResult=%d", message, result);
        return false;
    }
    return true;
}

VkSurfaceFormatKHR chooseSurfaceFormat(std::vector<VkSurfaceFormatKHR> const& formats)
{
    for (auto const& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(std::vector<VkPresentModeKHR> const& modes)
{
    for (auto const mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

bool MinimalVulkanTriangle::create(AAssetManager* assets, std::string project_path)
{
    assets_ = assets;
    project_path_ = std::move(project_path);
    logProjectAsset();

    return true;
    // return device_.CreateInstance(ave::rhi::VulkanDeviceConfig{}, "AveTriangleGame");
}

void MinimalVulkanTriangle::destroy()
{
   

     clearSurface();
    // if (surface_ != VK_NULL_HANDLE) {
    //     vkDestroySurfaceKHR(device_.Instance(), surface_, nullptr);
    //     surface_ = VK_NULL_HANDLE;
    // }
    // device_.Shutdown();
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

    if (!loadSceneMesh() || !createVertexBuffer() || !createRenderPass() || !createPipeline() || !createFramebuffers() ||
        !createCommandPoolAndBuffers()) {
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


bool MinimalVulkanTriangle::createRenderPass()
{
    vk::AttachmentDescription color_attachment{};
    color_attachment.format = swapchainWrap_.Format();
    color_attachment.samples = vk::SampleCountFlagBits::e1;
    color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
    color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
    color_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    color_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    color_attachment.initialLayout = vk::ImageLayout::eUndefined;
    color_attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    vk::SubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo create_info{};
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    try {
        render_pass_ = vk::raii::RenderPass(ctx_.Device(), create_info);
        return true;
    } catch (vk::SystemError& e) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createRenderPass failed: %s", e.what());
        return false;
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
            Vertex vertex{};
            std::copy(source.position.begin(), source.position.end(), vertex.position);
            std::copy(source.color.begin(), source.color.end(), vertex.color);
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

bool MinimalVulkanTriangle::createVertexBuffer()
{
    vk::DeviceSize const buffer_size = sizeof(Vertex) * vertices_.size();

    vk::BufferCreateInfo buffer_info{};
    buffer_info.size = buffer_size;
    buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
    buffer_info.sharingMode = vk::SharingMode::eExclusive;
    
    try {
        vertex_buffer_ = vk::raii::Buffer(ctx_.Device(), buffer_info);
    } catch (vk::SystemError& e) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createBuffer failed: %s", e.what());
        return false;
    }

    auto requirements = vertex_buffer_.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = requirements.size;
    auto memory_properties = ctx_.PhysicalDevice().getMemoryProperties();
    uint32_t memory_type_index = 0;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1 << i)) && 
            (memory_properties.memoryTypes[i].propertyFlags & 
             (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) ==
             (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) {
            memory_type_index = i;
            break;
        }
    }
    alloc_info.memoryTypeIndex = memory_type_index;

    try {
        vertex_memory_ = vk::raii::DeviceMemory(ctx_.Device(), alloc_info);
    } catch (vk::SystemError& e) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "allocateMemory failed: %s", e.what());
        return false;
    }

    vertex_buffer_.bindMemory(*vertex_memory_, 0);

    auto mapped = vertex_memory_.mapMemory(0, buffer_size);
    std::memcpy(mapped, vertices_.data(), static_cast<size_t>(buffer_size));
    vertex_memory_.unmapMemory();

    return true;
}

bool MinimalVulkanTriangle::createPipeline()
{
    auto vert_code = readShaderAsset("compiled_shaders/solid_triangle.vert.spv");
    auto frag_code = readShaderAsset("compiled_shaders/solid_triangle.frag.spv");
    if (vert_code.empty() || frag_code.empty()) {
        logError("Missing compiled triangle shaders.");
        return false;
    }

    vk::ShaderModuleCreateInfo vert_info{};
    vert_info.codeSize = vert_code.size() * sizeof(uint32_t);
    vert_info.pCode = vert_code.data();

    vk::ShaderModuleCreateInfo frag_info = vert_info;
    frag_info.codeSize = frag_code.size() * sizeof(uint32_t);
    frag_info.pCode = frag_code.data();

    // Create shader modules
    vk::raii::ShaderModule vert_module(ctx_.Device(), vert_info);
    vk::raii::ShaderModule frag_module(ctx_.Device(), frag_info);
    
    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = *vert_module;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = *frag_module;
    stages[1].pName = "main";

    vk::VertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = vk::Format::eR32G32B32Sfloat;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
    attributes[1].offset = offsetof(Vertex, color);

    vk::PipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport viewport{};
    auto extent = swapchainWrap_.Extent();
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.extent = extent;

    vk::PipelineViewportStateCreateInfo viewport_state{};
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    // Create pipeline layout
    vk::PipelineLayoutCreateInfo layout_info{};
    pipeline_layout_ = vk::raii::PipelineLayout(ctx_.Device(), layout_info);

    vk::GraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &blend;
    pipeline_info.layout = *pipeline_layout_;
    pipeline_info.renderPass = *render_pass_;
    pipeline_info.subpass = 0;

    // Create graphics pipeline
    pipeline_ = vk::raii::Pipeline(ctx_.Device(), nullptr, pipeline_info);
    
    // Note: Shader modules are automatically destroyed by raii when they go out of scope
    return true;
}

bool MinimalVulkanTriangle::createFramebuffers()
{
    std::vector<vk::ImageView> image_views;
    for (uint32_t i = 0; i < swapchainWrap_.ImageCount(); ++i) {
        image_views.push_back(swapchainWrap_.ImageView(i));
    }
    auto extent = swapchainWrap_.Extent();
    framebuffers_.reserve(image_views.size());
    for (auto const view : image_views) {
        vk::FramebufferCreateInfo create_info{};
        create_info.renderPass = *render_pass_;
        create_info.attachmentCount = 1;
        create_info.pAttachments = &view;
        create_info.width = extent.width;
        create_info.height = extent.height;
        create_info.layers = 1;

        try {
            framebuffers_.emplace_back(ctx_.Device(), create_info);
        } catch (vk::SystemError& e) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createFramebuffer failed: %s", e.what());
            return false;
        }
    }
    return true;
}

bool MinimalVulkanTriangle::createCommandPoolAndBuffers()
{
    vk::CommandPoolCreateInfo pool_info{};
    pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    pool_info.queueFamilyIndex = ctx_.GraphicsQueueFamilyIndex();
    
    try {
        command_pool_ = vk::raii::CommandPool(ctx_.Device(), pool_info);
    } catch (vk::SystemError& e) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createCommandPool failed: %s", e.what());
        return false;
    }

    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.commandPool = *command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = static_cast<uint32_t>(framebuffers_.size());
    
    try {
        command_buffers_ = vk::raii::CommandBuffers(ctx_.Device(), alloc_info);
        return true;
    } catch (vk::SystemError& e) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "allocateCommandBuffers failed: %s", e.what());
        return false;
    }
}


void MinimalVulkanTriangle::recordCommandBuffer(vk::raii::CommandBuffer& command_buffer, uint32_t image_index)
{
    vk::CommandBufferBeginInfo begin_info{};
    command_buffer.begin(begin_info);

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.03f;
    clear.color.float32[1] = 0.04f;
    clear.color.float32[2] = 0.06f;
    clear.color.float32[3] = 1.0f;

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.renderPass = *render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index];
    render_pass_info.renderArea.extent = swapchainWrap_.Extent();
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear;

    command_buffer.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline_);
    vk::DeviceSize offset = 0;
    command_buffer.bindVertexBuffers(0, *vertex_buffer_, offset);
    command_buffer.draw(static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
    command_buffer.endRenderPass();
    command_buffer.end();
}

void MinimalVulkanTriangle::drawFrame()
{
    if (!ctx_.IsInitialized() || swapchainWrap_.Handle() == vk::SwapchainKHR{} || command_buffers_.empty()) {
        return;
    }

    sync_.WaitForFrame(ctx_, frame_index_);
    auto [acq_result, image_index] = swapchainWrap_.AcquireNextImage(UINT64_MAX, sync_.ImageAvailable(frame_index_), vk::Fence{});
    if (acq_result == vk::Result::eErrorOutOfDateKHR)
      return;
    if (acq_result != vk::Result::eSuccess && acq_result != vk::Result::eSuboptimalKHR)
      throw std::runtime_error("acquireNextImage failed");

    sync_.ResetFence(ctx_, frame_index_);
    auto &cmd = command_buffers_.at(frame_index_);
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo{});

    recordCommandBuffer(cmd, image_index);
    cmd.end();
    swapchainWrap_.MarkUsed(image_index);

    vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    auto image_avail = sync_.ImageAvailable(frame_index_);
    submit.pWaitSemaphores = &image_avail;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    vk::CommandBuffer raw_cmd = *cmd;
    submit.pCommandBuffers = &raw_cmd;
    submit.signalSemaphoreCount = 1;
    auto render_finished = sync_.RenderFinished(frame_index_);
    submit.pSignalSemaphores = &render_finished;
    ctx_.GraphicsQueue().submit(submit, sync_.InFlightFence(frame_index_));

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    auto sc = swapchainWrap_.Handle();
    present.pSwapchains = &sc;
    present.pImageIndices = &image_index;
    auto pres_result = ctx_.GraphicsQueue().presentKHR(present);
    if (pres_result == vk::Result::eErrorOutOfDateKHR || pres_result == vk::Result::eSuboptimalKHR)
      return;
    if (pres_result != vk::Result::eSuccess)
      throw std::runtime_error("presentKHR failed");

    frame_index_ = (frame_index_ + 1) % sync_.FramesInFlight();
}

void MinimalVulkanTriangle::cleanupSurfaceResources()
{
    if (!ctx_.IsInitialized()) {
        return;
    }

    // Note: All raii objects are automatically destroyed when they go out of scope
    // framebuffers_, command_buffers_, command_pool_, pipeline_, pipeline_layout_, 
    // render_pass_, vertex_buffer_, and vertex_memory_ will be automatically cleaned up
    
    // Clear the containers to release raii objects
    framebuffers_.clear();
    command_buffers_.clear();
    
    // Note: swapchain images, image views, and sync objects are now managed by vkfw wrappers
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
