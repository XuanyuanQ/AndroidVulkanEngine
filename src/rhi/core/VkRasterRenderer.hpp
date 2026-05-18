#pragma once

#include "VkContext.hpp"
#include "VkSwapchain.hpp"
#include "VkFrameSync.hpp"
#include "VkPipeline.hpp"
#include "VkBuffer.hpp"
#include "VkShader.hpp"
#include "VkCommandBuffer.hpp"

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

  // External-resource initialization path:
  // Vertex buffers and pipeline are created by higher-level systems (ResourceSystem / PipelineSystem).
  bool InitializeWithExternalResources(vkfw::VkContext& ctx,
                                      vkfw::VkSwapchain& swapchain,
                                      vkfw::VkFrameSync& sync,
                                      vkfw::VkBuffer const* vertex_buffer,
                                      uint32_t vertex_count,
                                      vkfw::VkPipeline const* pipeline);
  void Shutdown();

  bool IsInitialized() const noexcept { return initialized_; }
  void RenderFrame(vkfw::VkContext& ctx,
                   vkfw::VkSwapchain& swapchain,
                   vkfw::VkFrameSync& sync,
                   uint32_t& frame_index);

private:
  bool createVertexBuffer(vkfw::VkContext& ctx, std::span<RasterColorVertex const> vertices);
  bool createPipeline(vkfw::VkContext& ctx,
                      vkfw::VkSwapchain& swapchain,
                      RasterShaderCode const& shaders);
  bool createCommandPoolAndBuffers(vkfw::VkContext& ctx, vkfw::VkFrameSync& sync);
  void recordCommandBuffer(vkfw::VkSwapchain& swapchain,
                           vk::CommandBuffer command_buffer,
                           uint32_t image_index);
  void destroyResources();

  vkfw::VkContext* ctx_ = nullptr;
  bool initialized_ = false;
  std::vector<RasterColorVertex> vertices_{};
  vkfw::VkBuffer const* external_vertex_buffer_ = nullptr;
  uint32_t external_vertex_count_ = 0;
  vkfw::VkPipeline const* external_pipeline_ = nullptr;
  vkfw::VkPipelineLayout pipeline_layout_{};
  vkfw::VkPipeline pipeline_{};
  vkfw::VkBuffer vertex_buffer_{};
  vkfw::VkCommandBuffer command_buffers_{};
};

} // namespace ave::rhi
