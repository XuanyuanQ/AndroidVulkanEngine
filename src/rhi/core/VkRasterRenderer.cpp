#include "VkRasterRenderer.hpp"

#include "LogUtil.h"
#include <cstring>
#include <stdexcept>

namespace ave::rhi {
namespace {

} // namespace

bool VulkanRasterRenderer::Initialize(vkfw::VkContext& ctx,
                                      vkfw::VkSwapchain& swapchain,
                                      vkfw::VkFrameSync& sync,
                                      std::span<RasterColorVertex const> vertices,
                                      RasterShaderCode const& shaders)
{
  Shutdown();
  ctx_ = &ctx;
  external_vertex_buffer_ = nullptr;
  external_pipeline_ = nullptr;
  external_vertex_count_ = 0;
  external_vertex_stride_ = 0;
  external_index_buffer_ = nullptr;
  external_index_count_ = 0;
  vertices_.assign(vertices.begin(), vertices.end());
  use_dynamic_rendering_ = ctx.SupportsDynamicRendering();

  if (!createVertexBuffer(ctx, vertices) ||
      !createRenderTargets(ctx, swapchain) ||
      !createPipeline(ctx, swapchain, shaders) ||
      !createCommandPoolAndBuffers(ctx, sync)) {
    destroyResources();
    return false;
  }

  initialized_ = true;
  return true;
}

bool VulkanRasterRenderer::InitializeWithExternalResources(vkfw::VkContext& ctx,
                                                          vkfw::VkSwapchain& swapchain,
                                                          vkfw::VkFrameSync& sync,
                                                          vkfw::VkBuffer const* vertex_buffer,
                                                          uint32_t vertex_count,
                                                          vkfw::VkPipeline const* pipeline)
{
  Shutdown();
  ctx_ = &ctx;
  vertices_.clear();
  external_vertex_buffer_ = vertex_buffer;
  external_vertex_count_ = vertex_count;
  external_vertex_stride_ = static_cast<uint32_t>(sizeof(RasterColorVertex));
  external_index_buffer_ = nullptr;
  external_index_count_ = 0;
  external_pipeline_ = pipeline;
  use_dynamic_rendering_ = ctx.SupportsDynamicRendering();

  if (external_vertex_buffer_ == nullptr || external_pipeline_ == nullptr || external_vertex_count_ == 0) {
    return false;
  }

  if (!createRenderTargets(ctx, swapchain) ||
      !createCommandPoolAndBuffers(ctx, sync)) {
    destroyResources();
    return false;
  }

  initialized_ = true;
  return true;
}

bool VulkanRasterRenderer::InitializeWithExternalBuffers(vkfw::VkContext& ctx,
                                                         vkfw::VkSwapchain& swapchain,
                                                         vkfw::VkFrameSync& sync,
                                                         vkfw::VkBuffer const* vertex_buffer,
                                                         uint32_t vertex_count,
                                                         uint32_t vertex_stride,
                                                         vkfw::VkBuffer const* index_buffer,
                                                         uint32_t index_count,
                                                         RasterShaderCode const& shaders)
{
  Shutdown();
  ctx_ = &ctx;
  vertices_.clear();
  external_vertex_buffer_ = vertex_buffer;
  external_vertex_count_ = vertex_count;
  external_vertex_stride_ = vertex_stride;
  external_index_buffer_ = index_buffer;
  external_index_count_ = index_count;
  external_pipeline_ = nullptr;
  use_dynamic_rendering_ = ctx.SupportsDynamicRendering();

  if (external_vertex_buffer_ == nullptr || external_vertex_count_ == 0) {
    return false;
  }

  if (!createRenderTargets(ctx, swapchain) ||
      !createPipeline(ctx, swapchain, shaders) ||
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
  ctx_ = nullptr;
  initialized_ = false;
  external_vertex_buffer_ = nullptr;
  external_vertex_count_ = 0;
  external_index_buffer_ = nullptr;
  external_index_count_ = 0;
  external_pipeline_ = nullptr;
}

void VulkanRasterRenderer::RenderFrame(vkfw::VkContext& ctx,
                                       vkfw::VkSwapchain& swapchain,
                                       vkfw::VkFrameSync& sync,
                                       uint32_t& frame_index)
{
  if (!initialized_ || swapchain.Handle() == vk::SwapchainKHR{} || command_buffers_.Count() == 0) {
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
  command_buffers_.Reset(frame_index);
  auto cmd = command_buffers_.Handle(frame_index);
  recordCommandBuffer(swapchain, cmd, image_index);
  swapchain.MarkUsed(image_index);

  vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  vk::SubmitInfo submit{};
  submit.waitSemaphoreCount = 1;
  auto image_avail = sync.ImageAvailable(frame_index);
  submit.pWaitSemaphores = &image_avail;
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
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


bool VulkanRasterRenderer::createVertexBuffer(vkfw::VkContext& ctx, std::span<RasterColorVertex const> vertices)
{
  uint32_t const buffer_size = static_cast<uint32_t>(sizeof(RasterColorVertex) * vertices.size());
  if (!vertex_buffer_.Init(ctx, vkfw::BufferInfo{
          .size = buffer_size,
          .usage = vkfw::BufferUsage::Vertex,
          .mappable = true,
      })) {
    LOGE("VkRasterRenderer: createVertexBuffer failed");
    return false;
  }

  vertex_buffer_.UpdateData(ctx, vertices.data(), buffer_size);
  return true;
}

bool VulkanRasterRenderer::createPipeline(vkfw::VkContext& ctx,
                                          vkfw::VkSwapchain& swapchain,
                                          RasterShaderCode const& shaders)
{
  if (shaders.vertex.empty() || shaders.fragment.empty()) {
    LOGE("VkRasterRenderer: createPipeline failed: shader code is empty");
    return false;
  }

  try {
    vkfw::VkShader vertex_shader{};
    if (!vertex_shader.Init(ctx, vkfw::ShaderInfo{
            .spirv_code = shaders.vertex,
            .stage = vkfw::ShaderStage::Vertex,
            .entry_point = "main",
        })) {
      LOGE("VkRasterRenderer: createPipeline failed: vertex shader init");
      return false;
    }

    vkfw::VkShader fragment_shader{};
    if (!fragment_shader.Init(ctx, vkfw::ShaderInfo{
            .spirv_code = shaders.fragment,
            .stage = vkfw::ShaderStage::Fragment,
            .entry_point = "main",
        })) {
      LOGE("VkRasterRenderer: createPipeline failed: fragment shader init");
      return false;
    }

    if (!pipeline_layout_.Init(ctx, {})) {
      LOGE("VkRasterRenderer: createPipeline failed: pipeline layout init");
      return false;
    }

    vk::Viewport viewport{};
    auto extent = swapchain.Extent();
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vk::Rect2D scissor{};
    scissor.extent = extent;

    vkfw::PipelineInfo pipeline_info{};
    pipeline_info.shader_stages.push_back(vertex_shader.GetPipelineStageInfo());
    pipeline_info.shader_stages.push_back(fragment_shader.GetPipelineStageInfo());
    uint32_t vertex_stride = static_cast<uint32_t>(sizeof(RasterColorVertex));
    uint32_t position_offset = static_cast<uint32_t>(offsetof(RasterColorVertex, position));
    uint32_t color_offset = static_cast<uint32_t>(offsetof(RasterColorVertex, color));
    if (external_vertex_buffer_ != nullptr && external_vertex_stride_ == sizeof(project::VertexData)) {
      vertex_stride = static_cast<uint32_t>(sizeof(project::VertexData));
      position_offset = static_cast<uint32_t>(offsetof(project::VertexData, position));
      color_offset = static_cast<uint32_t>(offsetof(project::VertexData, color));
    }
    pipeline_info.vertex_input.vertex_inputs = {
        vkfw::PipelineVertexInput{
            .binding = 0,
            .location = 0,
            .stride = vertex_stride,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = position_offset,
        },
        vkfw::PipelineVertexInput{
            .binding = 0,
            .location = 1,
            .stride = vertex_stride,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .offset = color_offset,
        },
    };
    pipeline_info.vertex_input.topology = vk::PrimitiveTopology::eTriangleList;
    pipeline_info.layout = pipeline_layout_.Handle();
    pipeline_info.use_dynamic_rendering = use_dynamic_rendering_;
    if (use_dynamic_rendering_) {
      pipeline_info.color_formats = {swapchain.Format()};
    } else {
      pipeline_info.render_pass = render_pass_.Handle();
    }
    pipeline_info.viewport.viewports = {viewport};
    pipeline_info.viewport.scissors = {scissor};
    pipeline_info.rasterization.polygon_mode = vk::PolygonMode::eFill;
    pipeline_info.rasterization.cull_mode = vk::CullModeFlagBits::eNone;
    pipeline_info.rasterization.front_face = vk::FrontFace::eCounterClockwise;
    pipeline_info.multisample.rasterization_samples = vk::SampleCountFlagBits::e1;
    pipeline_info.depth_stencil.depth_test_enable = false;
    pipeline_info.depth_stencil.depth_write_enable = false;
    pipeline_info.color_blend.attachments = {vkfw::PipelineColorBlendAttachment{}};
    if (!pipeline_.Init(ctx, pipeline_info)) {
      LOGE("VkRasterRenderer: createPipeline failed: pipeline init");
      pipeline_layout_.Shutdown(ctx);
      return false;
    }
    return true;
  } catch (std::exception const& e) {
    LOGE("VkRasterRenderer: createPipeline failed: %s", e.what());
    return false;
  }
}

bool VulkanRasterRenderer::createRenderTargets(vkfw::VkContext& ctx, vkfw::VkSwapchain& swapchain)
{
  if (use_dynamic_rendering_) {
    return true;
  }

  vkfw::RenderPassAttachment color_attachment{};
  color_attachment.binding = 0;
  color_attachment.type = vkfw::RenderPassAttachmentType::Color;
  color_attachment.format = swapchain.Format();
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.load_op = vkfw::RenderPassLoadOp::Clear;
  color_attachment.store_op = vkfw::RenderPassStoreOp::Store;
  color_attachment.initial_layout = vk::ImageLayout::eColorAttachmentOptimal;
  color_attachment.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

  vkfw::RenderPassSubpass subpass{};
  subpass.color_attachments.push_back(color_attachment);

  vkfw::RenderPassInfo render_pass_info{};
  render_pass_info.subpasses.push_back(subpass);
  render_pass_info.final_layout = vk::ImageLayout::eColorAttachmentOptimal;

  if (!render_pass_.Init(ctx, render_pass_info)) {
    LOGE("VkRasterRenderer: createRenderTargets failed: render pass init");
    return false;
  }

  if (!framebuffers_.Init(ctx, swapchain, render_pass_)) {
    LOGE("VkRasterRenderer: createRenderTargets failed: framebuffer init");
    render_pass_.Shutdown(ctx);
    return false;
  }

  return true;
}

bool VulkanRasterRenderer::createCommandPoolAndBuffers(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync)
{
  if (!command_buffers_.Init(ctx, vkfw::CommandBufferInfo{
          .level = vkfw::CommandBufferLevel::Primary,
          .usage = vkfw::CommandBufferUsage::OneTimeSubmit,
          .count = sync.FramesInFlight(),
      })) {
    LOGE("VkRasterRenderer: createCommandPoolAndBuffers failed");
    return false;
  }
  return true;
}

void VulkanRasterRenderer::recordCommandBuffer(vkfw::VkSwapchain& swapchain,
                                               vk::CommandBuffer command_buffer,
                                               uint32_t image_index)
{
  command_buffer.begin(vk::CommandBufferBeginInfo{});

  vk::Image const swap_img = swapchain.Image(image_index);
  vk::ImageSubresourceRange const range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  vk::ImageLayout const old_layout = swapchain.IsFirstUse(image_index) ? vk::ImageLayout::eUndefined
                                                                       : vk::ImageLayout::ePresentSrcKHR;

  vk::ImageMemoryBarrier to_color{};
  to_color.oldLayout = old_layout;
  to_color.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
  to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_color.image = swap_img;
  to_color.subresourceRange = range;
  to_color.srcAccessMask = {};
  to_color.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                 vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                 {}, {}, {}, to_color);

  vk::ClearValue clear{};
  clear.color.float32[0] = 1.0f;
  clear.color.float32[1] = 1.0f;
  clear.color.float32[2] = 1.0f;
  clear.color.float32[3] = 1.0f;

  if (use_dynamic_rendering_) {
    bool const core_dynamic_rendering =
        ctx_ != nullptr && ctx_->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    vk::RenderingAttachmentInfo color_attachment{};
    if (core_dynamic_rendering) {
      color_attachment.imageView = swapchain.ImageView(image_index);
      color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
      color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
      color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
      color_attachment.clearValue = clear;

      vk::RenderingInfo rendering_info{};
      rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
      rendering_info.layerCount = 1;
      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &color_attachment;

      command_buffer.beginRendering(rendering_info);
    } else {
      vk::RenderingAttachmentInfoKHR color_attachment_khr{};
      color_attachment_khr.imageView = swapchain.ImageView(image_index);
      color_attachment_khr.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
      color_attachment_khr.loadOp = vk::AttachmentLoadOp::eClear;
      color_attachment_khr.storeOp = vk::AttachmentStoreOp::eStore;
      color_attachment_khr.clearValue = clear;

      vk::RenderingInfoKHR rendering_info{};
      rendering_info.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
      rendering_info.layerCount = 1;
      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &color_attachment_khr;

      command_buffer.beginRenderingKHR(rendering_info);
    }
  } else {
    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = render_pass_.Handle();
    render_pass_begin.framebuffer = framebuffers_.Handle(image_index);
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, swapchain.Extent()};
    render_pass_begin.clearValueCount = 1;
    render_pass_begin.pClearValues = &clear;
    command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
  }
  if (external_pipeline_ != nullptr) {
    command_buffer.bindPipeline(external_pipeline_->BindPoint(), external_pipeline_->Handle());
  } else {
    command_buffer.bindPipeline(pipeline_.BindPoint(), pipeline_.Handle());
  }
  vk::DeviceSize offset = 0;
  auto vertex_buffer = external_vertex_buffer_ != nullptr ? external_vertex_buffer_->Handle() : vertex_buffer_.Handle();
  command_buffer.bindVertexBuffers(0, vertex_buffer, offset);
  if (external_index_buffer_ != nullptr && external_index_count_ > 0) {
    command_buffer.bindIndexBuffer(external_index_buffer_->Handle(), 0, vk::IndexType::eUint32);
    command_buffer.drawIndexed(external_index_count_, 1, 0, 0, 0);
  } else {
    uint32_t vertex_count = external_vertex_buffer_ != nullptr ? external_vertex_count_ : static_cast<uint32_t>(vertices_.size());
    command_buffer.draw(vertex_count, 1, 0, 0);
  }
  if (use_dynamic_rendering_) {
    bool const core_dynamic_rendering =
        ctx_ != nullptr && ctx_->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (core_dynamic_rendering) {
      command_buffer.endRendering();
    } else {
      command_buffer.endRenderingKHR();
    }
  } else {
    command_buffer.endRenderPass();
  }

  vk::ImageMemoryBarrier to_present{};
  to_present.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  to_present.newLayout = vk::ImageLayout::ePresentSrcKHR;
  to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_present.image = swap_img;
  to_present.subresourceRange = range;
  to_present.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  to_present.dstAccessMask = {};
  command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                 vk::PipelineStageFlagBits::eBottomOfPipe,
                                 {}, {}, {}, to_present);
  command_buffer.end();
}

void VulkanRasterRenderer::destroyResources()
{
  if (ctx_ != nullptr) {
    command_buffers_.Shutdown(*ctx_);
    framebuffers_.Shutdown(*ctx_);
    render_pass_.Shutdown(*ctx_);
    pipeline_.Shutdown(*ctx_);
    pipeline_layout_.Shutdown(*ctx_);
    if (vertex_buffer_.MappedData() != nullptr) {
      vertex_buffer_.Unmap(*ctx_);
    }
    vertex_buffer_.Shutdown(*ctx_);
  } else {
    command_buffers_ = {};
  }
  vertices_.clear();
  external_vertex_buffer_ = nullptr;
  external_pipeline_ = nullptr;
  external_vertex_count_ = 0;
}

} // namespace ave::rhi
