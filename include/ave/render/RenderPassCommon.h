#pragma once

#include "ave/render/RenderPass.h"
#include "ave/project/SharedDataContract.h"
#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkContext.hpp"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "VkSwapchain.hpp"
#include "LogUtil.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ave::render::detail {

struct FrameUbo {
    glm::mat4 view_projection{1.0f};
    glm::mat4 shadow_view_projection{1.0f};
    glm::vec4 camera_position{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 light_position_range{0.0f, 6.0f, 6.0f, 20.0f};
    glm::vec4 light_direction_type{0.0f, -1.0f, 0.0f, 1.0f};
    glm::vec4 light_color_intensity{1.0f, 1.0f, 1.0f, 5.0f};
    glm::vec4 ambient_color{0.04f, 0.04f, 0.045f, 1.0f};
    glm::vec4 clear_color{0.03f, 0.04f, 0.06f, 1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

struct SharedEnvironmentMaps {
    vkfw::VkTexture environment_cubemap{};
    vkfw::VkTexture irradiance_cubemap{};
    vkfw::VkTexture prefilter_cubemap{};
    vkfw::VkTexture brdf_lut{};
    glm::vec4 last_clear_color{-1.0f};
    glm::vec3 last_ambient_color{-1.0f};
    bool last_use_cubemap_source = false;
    bool ready = false;
};

struct CpuCubemapFace {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<glm::vec4> pixels;
};

struct CpuCubemapSource {
    std::array<CpuCubemapFace, 6> faces{};
    bool ready = false;
};

inline constexpr uint32_t kShadowMapSize = 1024;

extern std::vector<uint32_t> g_culling_visibility;
extern uint32_t g_culling_shader_id;
extern std::unique_ptr<vk::raii::RenderPass> g_compatibility_shadow_render_pass;
extern std::unique_ptr<vk::raii::Framebuffer> g_compatibility_shadow_framebuffer;
extern vk::ImageView g_last_shadow_image_view;
extern SharedEnvironmentMaps g_shared_environment_maps;

vk::Sampler GetCommonSampler(vkfw::VkContext& ctx);
vk::Sampler GetShadowSampler(vkfw::VkContext& ctx);
void ResetCommonSampler();
void ResetShadowSampler();

void EnsureFallbackWhiteTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture);
void EnsureFallbackNormalTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture);

vkfw::VkTexture const* ResolveTextureOrFallback(vkfw::VkContext& ctx,
                                                ave::resource::TextureManager& texture_mgr,
                                                uint32_t texture_id,
                                                vkfw::VkTexture& fallback_texture);
vkfw::VkTexture const* ResolveNormalTextureOrFallback(vkfw::VkContext& ctx,
                                                      ave::resource::TextureManager& texture_mgr,
                                                      uint32_t texture_id,
                                                      vkfw::VkTexture& fallback_texture);

glm::mat4 BuildShadowViewProjection(PassExecutionView const& view, core::FrameData const* frame);

void TransitionImageLayout(vk::CommandBuffer const& command_buffer,
                           vk::Image image,
                           vk::ImageAspectFlags aspect_mask,
                           vk::ImageLayout old_layout,
                           vk::ImageLayout new_layout,
                           vk::AccessFlags src_access_mask,
                           vk::AccessFlags dst_access_mask,
                           vk::PipelineStageFlags src_stage,
                           vk::PipelineStageFlags dst_stage);

bool BeginShadowMapRendering(RenderPassContext const& context,
                             vkfw::VkTexture const& shadow_map,
                             uint32_t shadow_map_size,
                             vk::ClearDepthStencilValue const& clear_depth);
bool BeginDepthOnlyRendering(RenderPassContext const& context,
                             vkfw::VkTexture const& depth_texture,
                             vk::Extent2D extent,
                             vk::ClearDepthStencilValue const& clear_depth);
void EndShadowMapRendering(RenderPassContext const& context);
bool BeginSwapchainRendering(RenderPassContext const& context,
                             vk::ClearValue const& clear_value,
                             bool clear_color,
                             vkfw::VkTexture const* depth_texture = nullptr,
                             bool clear_depth = true);
void EndSwapchainRendering(RenderPassContext const& context);

void AppendUiQuad(std::vector<ave::render::UiVertex>& vertices,
                  std::vector<uint32_t>& indices,
                  core::FrameUiData const& item,
                  uint32_t texture_index,
                  float aspect_ratio);

PipelineKey MakePipelineKey(uint32_t shader_id, ave::resource::MeshRuntime const& mesh);

void EnsureSharedEnvironmentMaps(vkfw::VkContext& ctx,
                                 resource::ResourceSystem* resources,
                                 glm::vec4 const& clear_color,
                                 glm::vec3 const& ambient_color);

} // namespace ave::render::detail
