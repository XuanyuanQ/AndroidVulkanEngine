#include "VulkanRasterRenderer.hpp"

#include <android/log.h>
#include <cstring>
#include <stdexcept>

namespace ave::rhi {
namespace {

constexpr char kLogTag[] = "AveRuntime";

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

bool VulkanRasterRenderer::Initialize(vkfw::VkContext& ctx,
                                      vkfw::VkSwapchain& swapchain,
                                      vkfw::VkFrameSync& sync,
                                      std::span<RasterColorVertex const> vertices,
                                      RasterShaderCode const& shaders)
{
  Shutdown();
  vertices_.assign(vertices.begin(), vertices.end());

  if (!createVertexBuffer(ctx, vertices) ||
      !createRenderPass(ctx, swapchain) ||
      !createPipeline(ctx, swapchain, shaders) ||
      !createFramebuffers(ctx, swapchain) ||
      !createCommandPoolAndBuffers(ctx, sync)) {
    destroyResources();
    return false;
  }

  initialized_ = true;
  return true;
}

void VulkanRasterRenderer::Shutdown()
{
  destroyResources();
  initialized_ = false;
}

void VulkanRasterRenderer::RenderFrame(vkfw::VkContext& ctx,
                                       vkfw::VkSwapchain& swapchain,
                                       vkfw::VkFrameSync& sync,
                                       uint32_t& frame_index)
{
  if (!initialized_ || swapchain.Handle() == vk::SwapchainKHR{} || command_buffers_.empty()) {
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
  auto& cmd = command_buffers_.at(frame_index);
  cmd.reset();
  recordCommandBuffer(swapchain, cmd, image_index);
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
  auto render_finished = sync.RenderFinished(frame_index);
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

bool VulkanRasterRenderer::createRenderPass(vkfw::VkContext& ctx, vkfw::VkSwapchain& swapchain)
{
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

  vk::RenderPassCreateInfo create_info{};
  create_info.attachmentCount = 1;
  create_info.pAttachments = &color_attachment;
  create_info.subpassCount = 1;
  create_info.pSubpasses = &subpass;
  create_info.dependencyCount = 1;
  create_info.pDependencies = &dependency;

  try {
    render_pass_ = vk::raii::RenderPass(ctx.Device(), create_info);
    return true;
  } catch (vk::SystemError& e) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createRenderPass failed: %s", e.what());
    return false;
  }
}

bool VulkanRasterRenderer::createVertexBuffer(vkfw::VkContext& ctx, std::span<RasterColorVertex const> vertices)
{
  vk::DeviceSize buffer_size = sizeof(RasterColorVertex) * vertices.size();

  vk::BufferCreateInfo buffer_info{};
  buffer_info.size = buffer_size;
  buffer_info.usage = vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_info.sharingMode = vk::SharingMode::eExclusive;

  try {
    vertex_buffer_ = vk::raii::Buffer(ctx.Device(), buffer_info);
    auto requirements = vertex_buffer_.getMemoryRequirements();

    vk::MemoryAllocateInfo alloc_info{};
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = FindHostVisibleMemoryType(ctx, requirements.memoryTypeBits);
    vertex_memory_ = vk::raii::DeviceMemory(ctx.Device(), alloc_info);
    vertex_buffer_.bindMemory(*vertex_memory_, 0);

    auto mapped = vertex_memory_.mapMemory(0, buffer_size);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(buffer_size));
    vertex_memory_.unmapMemory();
    return true;
  } catch (std::exception const& e) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createVertexBuffer failed: %s", e.what());
    return false;
  }
}

bool VulkanRasterRenderer::createPipeline(vkfw::VkContext& ctx,
                                          vkfw::VkSwapchain& swapchain,
                                          RasterShaderCode const& shaders)
{
  if (shaders.vertex.empty() || shaders.fragment.empty()) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createPipeline failed: shader code is empty");
    return false;
  }

  try {
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

    pipeline_layout_ = vk::raii::PipelineLayout(ctx.Device(), vk::PipelineLayoutCreateInfo{});

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

    pipeline_ = vk::raii::Pipeline(ctx.Device(), nullptr, pipeline_info);
    return true;
  } catch (std::exception const& e) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createPipeline failed: %s", e.what());
    return false;
  }
}

bool VulkanRasterRenderer::createFramebuffers(vkfw::VkContext& ctx, vkfw::VkSwapchain& swapchain)
{
  auto extent = swapchain.Extent();
  framebuffers_.clear();
  framebuffers_.reserve(swapchain.ImageCount());
  try {
    for (uint32_t i = 0; i < swapchain.ImageCount(); ++i) {
      auto view = swapchain.ImageView(i);
      vk::FramebufferCreateInfo create_info{};
      create_info.renderPass = *render_pass_;
      create_info.attachmentCount = 1;
      create_info.pAttachments = &view;
      create_info.width = extent.width;
      create_info.height = extent.height;
      create_info.layers = 1;
      framebuffers_.emplace_back(ctx.Device(), create_info);
    }
    return true;
  } catch (std::exception const& e) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createFramebuffers failed: %s", e.what());
    return false;
  }
}

bool VulkanRasterRenderer::createCommandPoolAndBuffers(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync)
{
  vk::CommandPoolCreateInfo pool_info{};
  pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  pool_info.queueFamilyIndex = ctx.GraphicsQueueFamilyIndex();

  try {
    command_pool_ = vk::raii::CommandPool(ctx.Device(), pool_info);
    vk::CommandBufferAllocateInfo alloc_info{};
    alloc_info.commandPool = *command_pool_;
    alloc_info.level = vk::CommandBufferLevel::ePrimary;
    alloc_info.commandBufferCount = sync.FramesInFlight();
    command_buffers_ = vk::raii::CommandBuffers(ctx.Device(), alloc_info);
    return true;
  } catch (std::exception const& e) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "createCommandPoolAndBuffers failed: %s", e.what());
    return false;
  }
}

void VulkanRasterRenderer::recordCommandBuffer(vkfw::VkSwapchain& swapchain,
                                               vk::raii::CommandBuffer& command_buffer,
                                               uint32_t image_index)
{
  command_buffer.begin(vk::CommandBufferBeginInfo{});

  vk::ClearValue clear{};
  clear.color.float32[0] = 0.03f;
  clear.color.float32[1] = 0.04f;
  clear.color.float32[2] = 0.06f;
  clear.color.float32[3] = 1.0f;

  vk::RenderPassBeginInfo render_pass_info{};
  render_pass_info.renderPass = *render_pass_;
  render_pass_info.framebuffer = *framebuffers_.at(image_index);
  render_pass_info.renderArea.extent = swapchain.Extent();
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

void VulkanRasterRenderer::destroyResources()
{
  framebuffers_.clear();
  command_buffers_.clear();
  command_pool_ = nullptr;
  pipeline_ = nullptr;
  pipeline_layout_ = nullptr;
  render_pass_ = nullptr;
  vertex_buffer_ = nullptr;
  vertex_memory_ = nullptr;
  vertices_.clear();
}

} // namespace ave::rhi
