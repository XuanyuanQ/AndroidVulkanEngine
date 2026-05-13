#include "ave/render/Renderer.h"

#include "VkContext.hpp"
#include "VkFrameSync.hpp"
#include "VkSwapchain.hpp"

#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <cstdio>
#endif
#include <cstring>
#include <stdexcept>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace ave::render {

namespace {

constexpr char kLogTag[] = "AveRuntime";

void LogError(char const* message)
{
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
#else
    std::fprintf(stderr, "%s: %s\n", kLogTag, message);
#endif
}

void LogErrorFmt(char const* prefix, char const* message)
{
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s: %s", prefix, message);
#else
    std::fprintf(stderr, "%s: %s: %s\n", kLogTag, prefix, message);
#endif
}

uint32_t FindHostVisibleMemoryType(vkfw::VkContext& ctx, uint32_t memory_type_bits)
{
    auto memory_properties = ctx.PhysicalDevice().getMemoryProperties();
    auto required = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        if ((memory_type_bits & (1u << i)) &&
            (memory_properties.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    throw std::runtime_error("No suitable host visible Vulkan memory type found");
}

} // namespace

class Renderer::Impl {
public:
    bool initialized = false;
    std::vector<RasterColorVertex> vertices;
    vk::raii::RenderPass render_pass{nullptr};
    vk::raii::PipelineLayout pipeline_layout{nullptr};
    vk::raii::Pipeline pipeline{nullptr};
    vk::raii::Buffer vertex_buffer{nullptr};
    vk::raii::DeviceMemory vertex_memory{nullptr};
    vk::raii::CommandPool command_pool{nullptr};
    std::vector<vk::raii::CommandBuffer> command_buffers;
    std::vector<vk::raii::Framebuffer> framebuffers;
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

bool Renderer::Initialize(RendererConfig const& config, resource::GpuUploadQueue& uploads)
{
    uploads_ = &uploads;
    return device_.Initialize(rhi::VulkanDeviceConfig{config.enable_validation});
}

void Renderer::Shutdown()
{
    ShutdownRaster();
    device_.Shutdown();
    uploads_ = nullptr;
}

void Renderer::Render(core::FrameData const& frame, core::JobSystem& jobs)
{
    if (uploads_ != nullptr) {
        auto uploads = uploads_->Drain();
        for (auto const& upload : uploads) {
            device_.SubmitDebugWork(upload.debug_name, 1);
        }
    }

    auto recorded = recorder_.RecordSceneParallel(frame, jobs);
    device_.SubmitDebugWork("FrameData secondary command buffers", static_cast<uint32_t>(recorded.size()));
    graph_.Execute(RenderPassContext{&frame});
}

bool Renderer::InitializeRaster(vkfw::VkContext& ctx,
                                vkfw::VkSwapchain& swapchain,
                                vkfw::VkFrameSync& sync,
                                std::span<RasterColorVertex const> vertices,
                                RasterShaderCode const& shaders)
{
    ShutdownRaster();
    impl_ = std::make_unique<Impl>();
    impl_->vertices.assign(vertices.begin(), vertices.end());

    try {
        vk::DeviceSize buffer_size = sizeof(RasterColorVertex) * impl_->vertices.size();

        vk::BufferCreateInfo buffer_info{};
        buffer_info.size = buffer_size;
        buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        buffer_info.sharingMode = vk::SharingMode::eExclusive;
        impl_->vertex_buffer = vk::raii::Buffer(ctx.Device(), buffer_info);

        auto requirements = impl_->vertex_buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo alloc_info{};
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = FindHostVisibleMemoryType(ctx, requirements.memoryTypeBits);
        impl_->vertex_memory = vk::raii::DeviceMemory(ctx.Device(), alloc_info);
        impl_->vertex_buffer.bindMemory(*impl_->vertex_memory, 0);

        auto mapped = impl_->vertex_memory.mapMemory(0, buffer_size);
        std::memcpy(mapped, impl_->vertices.data(), static_cast<size_t>(buffer_size));
        impl_->vertex_memory.unmapMemory();

        vk::AttachmentDescription color_attachment{};
        color_attachment.format = swapchain.Format();
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

        vk::RenderPassCreateInfo render_pass_info{};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &color_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;
        impl_->render_pass = vk::raii::RenderPass(ctx.Device(), render_pass_info);

        if (shaders.vertex.empty() || shaders.fragment.empty()) {
            throw std::runtime_error("Shader code is empty");
        }

        vk::ShaderModuleCreateInfo vert_info{};
        vert_info.codeSize = shaders.vertex.size() * sizeof(uint32_t);
        vert_info.pCode = shaders.vertex.data();
        vk::ShaderModuleCreateInfo frag_info{};
        frag_info.codeSize = shaders.fragment.size() * sizeof(uint32_t);
        frag_info.pCode = shaders.fragment.data();
        vk::raii::ShaderModule vert_module(ctx.Device(), vert_info);
        vk::raii::ShaderModule frag_module(ctx.Device(), frag_info);

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = *vert_module;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = *frag_module;
        stages[1].pName = "main";

        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(RasterColorVertex);
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::array<vk::VertexInputAttributeDescription, 2> attributes{};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(RasterColorVertex, position);
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
        attributes[1].offset = offsetof(RasterColorVertex, color);

        vk::PipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        vk::PipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        auto extent = swapchain.Extent();
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
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo blend{};
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        impl_->pipeline_layout = vk::raii::PipelineLayout(ctx.Device(), vk::PipelineLayoutCreateInfo{});

        vk::GraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pColorBlendState = &blend;
        pipeline_info.layout = *impl_->pipeline_layout;
        pipeline_info.renderPass = *impl_->render_pass;
        pipeline_info.subpass = 0;
        impl_->pipeline = vk::raii::Pipeline(ctx.Device(), nullptr, pipeline_info);

        impl_->framebuffers.reserve(swapchain.ImageCount());
        for (uint32_t i = 0; i < swapchain.ImageCount(); ++i) {
            auto view = swapchain.ImageView(i);
            vk::FramebufferCreateInfo framebuffer_info{};
            framebuffer_info.renderPass = *impl_->render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = &view;
            framebuffer_info.width = extent.width;
            framebuffer_info.height = extent.height;
            framebuffer_info.layers = 1;
            impl_->framebuffers.emplace_back(ctx.Device(), framebuffer_info);
        }

        vk::CommandPoolCreateInfo pool_info{};
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        pool_info.queueFamilyIndex = ctx.GraphicsQueueFamilyIndex();
        impl_->command_pool = vk::raii::CommandPool(ctx.Device(), pool_info);

        vk::CommandBufferAllocateInfo alloc_info_cb{};
        alloc_info_cb.commandPool = *impl_->command_pool;
        alloc_info_cb.level = vk::CommandBufferLevel::ePrimary;
        alloc_info_cb.commandBufferCount = sync.FramesInFlight();
        impl_->command_buffers = vk::raii::CommandBuffers(ctx.Device(), alloc_info_cb);
        impl_->initialized = true;
        return true;
    } catch (std::exception const& e) {
        LogErrorFmt("InitializeRaster failed", e.what());
        ShutdownRaster();
        return false;
    }
}

void Renderer::ShutdownRaster()
{
    impl_.reset();
}

void Renderer::RenderRasterFrame(vkfw::VkContext& ctx,
                                 vkfw::VkSwapchain& swapchain,
                                 vkfw::VkFrameSync& sync,
                                 uint32_t& frame_index)
{
    if (impl_ == nullptr || !impl_->initialized || swapchain.Handle() == vk::SwapchainKHR{} || impl_->command_buffers.empty()) {
        return;
    }

    sync.WaitForFrame(ctx, frame_index);
    auto [acq_result, image_index] = swapchain.AcquireNextImage(UINT64_MAX, sync.ImageAvailable(frame_index), vk::Fence{});
    if (acq_result == vk::Result::eErrorOutOfDateKHR) {
        return;
    }
    if (acq_result != vk::Result::eSuccess && acq_result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("acquireNextImage failed");
    }

    sync.ResetFence(ctx, frame_index);
    auto& cmd = impl_->command_buffers.at(frame_index);
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo{});

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.03f;
    clear.color.float32[1] = 0.04f;
    clear.color.float32[2] = 0.06f;
    clear.color.float32[3] = 1.0f;

    vk::RenderPassBeginInfo render_pass_info{};
    render_pass_info.renderPass = *impl_->render_pass;
    render_pass_info.framebuffer = *impl_->framebuffers.at(image_index);
    render_pass_info.renderArea.extent = swapchain.Extent();
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear;

    cmd.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *impl_->pipeline);
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, *impl_->vertex_buffer, offset);
    cmd.draw(static_cast<uint32_t>(impl_->vertices.size()), 1, 0, 0);
    cmd.endRenderPass();
    cmd.end();

    swapchain.MarkUsed(image_index);

    vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submit{};
    submit.waitSemaphoreCount = 1;
    auto image_avail = sync.ImageAvailable(frame_index);
    submit.pWaitSemaphores = &image_avail;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    vk::CommandBuffer raw_cmd = *cmd;
    submit.pCommandBuffers = &raw_cmd;
    submit.signalSemaphoreCount = 1;
    auto render_finished = sync.RenderFinished(image_index);
    submit.pSignalSemaphores = &render_finished;
    ctx.GraphicsQueue().submit(submit, sync.InFlightFence(frame_index));

    vk::PresentInfoKHR present{};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    auto handle = swapchain.Handle();
    present.pSwapchains = &handle;
    present.pImageIndices = &image_index;
    auto pres_result = ctx.GraphicsQueue().presentKHR(present);
    if (pres_result == vk::Result::eErrorOutOfDateKHR || pres_result == vk::Result::eSuboptimalKHR) {
        return;
    }
    if (pres_result != vk::Result::eSuccess) {
        throw std::runtime_error("presentKHR failed");
    }

    frame_index = (frame_index + 1) % sync.FramesInFlight();
}

FrameGraph& Renderer::Graph() noexcept
{
    return graph_;
}

rhi::VulkanDevice& Renderer::Device() noexcept
{
    return device_;
}

} // namespace ave::render
