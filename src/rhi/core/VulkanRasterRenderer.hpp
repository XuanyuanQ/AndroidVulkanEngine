#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "VkRenderPass.hpp"
#include "VkPipeline.hpp"
#include "VkBuffer.hpp"
#include "VkShader.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace ave::rhi {

struct RasterColorVertex {
  std::array<float, 3> position{0.0f, 0.0f, 0.0f};
  std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct RasterShaderCode {
  std::vector<uint32_t> vertex{};
  std::vector<uint32_t> fragment{};
};

class VulkanRasterRenderer {
public:
  bool Initialize(vkfw::VkContext& ctx,
                  vkfw::VkSwapchain& swapchain,
                  vkfw::VkFrameSync& sync,
                  std::span<RasterColorVertex const> vertices,
                  RasterShaderCode const& shaders);
  void Shutdown();

  bool IsInitialized() const noexcept { return initialized_; }
  void RenderFrame(vkfw::VkContext& ctx,
                   vkfw::VkSwapchain& swapchain,
                   vkfw::VkFrameSync& sync,
                   uint32_t& frame_index);

private:
  bool createRenderPass(vkfw::VkContext& ctx, vkfw::VkSwapchain& swapchain);
  bool createVertexBuffer(vkfw::VkContext& ctx, std::span<RasterColorVertex const> vertices);
  bool createPipeline(vkfw::VkContext& ctx,
                      vkfw::VkSwapchain& swapchain,
                      RasterShaderCode const& shaders);
  bool createFramebuffers(vkfw::VkContext& ctx, vkfw::VkSwapchain& swapchain);
  bool createCommandPoolAndBuffers(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync);
  void recordCommandBuffer(vkfw::VkSwapchain& swapchain,
                           vk::raii::CommandBuffer& command_buffer,
                           uint32_t image_index);
  void destroyResources();

  bool initialized_ = false;
  std::vector<RasterColorVertex> vertices_{};
  vkfw::VkRenderPass render_pass_;
  vk::raii::PipelineLayout pipeline_layout_{nullptr};
  vk::raii::Pipeline pipeline_{nullptr};
  vk::raii::Buffer vertex_buffer_{nullptr};
  vk::raii::DeviceMemory vertex_memory_{nullptr};
  vk::raii::CommandPool command_pool_{nullptr};
  std::vector<vk::raii::CommandBuffer> command_buffers_{};
  std::vector<vk::raii::Framebuffer> framebuffers_{};
};

} // namespace ave::rhi
