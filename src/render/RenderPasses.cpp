#include "ave/render/RenderPasses.h"

#include "ave/project/SharedDataContract.h"
#include "ave/render/PipelineSystem.h"
#include "ave/resource/ResourceSystem.h"
#include "VkContext.hpp"
#include "VkDescriptor.hpp"
#include "VkPipeline.hpp"
#include "VkSwapchain.hpp"
#include <android/log.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <algorithm>

namespace ave::render {
namespace {
// --- GPU Compute Culling Global Shared State ---
static std::vector<uint32_t> g_culling_visibility;
static uint32_t g_culling_shader_id = 0;
// ----------------------------------------------

vk::Sampler GetCommonSampler(vkfw::VkContext& ctx)
{
    static std::unique_ptr<vk::raii::Sampler> sampler;
    if (!sampler) {
        vk::SamplerCreateInfo create_info{};
        create_info.magFilter = vk::Filter::eLinear;
        create_info.minFilter = vk::Filter::eLinear;
        create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeW = vk::SamplerAddressMode::eRepeat;
        create_info.maxLod = VK_LOD_CLAMP_NONE;
        sampler = std::make_unique<vk::raii::Sampler>(ctx.Device(), create_info);
    }
    return **sampler;
}

void EnsureFallbackWhiteTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture)
{
    if (texture.IsInitialized()) {
        return;
    }

    uint32_t const white_pixel = 0xFFFFFFFFu;
    texture.Init(ctx, vkfw::TextureInfo{
                          .width = 1,
                          .height = 1,
                          .mip_levels = 1,
                          .format = vkfw::TextureFormat::R8G8B8A8_UNORM,
                          .usage = vkfw::TextureUsage::Sampled,
                          .mipmap = false,
                      });
    texture.UpdateData(ctx, &white_pixel, sizeof(white_pixel));
}

vkfw::VkTexture const* ResolveTextureOrFallback(vkfw::VkContext& ctx,
                                                ave::resource::TextureManager& texture_mgr,
                                                uint32_t texture_id,
                                                vkfw::VkTexture& fallback_texture)
{
    if (texture_id != 0) {
        if (auto const* runtime = texture_mgr.GetTexture(texture_id)) {
            if (runtime->texture && runtime->texture->IsInitialized()) {
                return runtime->texture.get();
            }
        }
    }

    EnsureFallbackWhiteTexture(ctx, fallback_texture);
    return fallback_texture.IsInitialized() ? &fallback_texture : nullptr;
}

uint32_t VertexLayoutIdFromMesh(ave::resource::MeshRuntime const& mesh)
{
    if (mesh.vertex_stride == 7u * sizeof(float)) {
        return 1;
    }
    if (mesh.vertex_stride == sizeof(ave::project::VertexData)) {
        return 2;
    }
    return 0;
}

DescriptorSetLayoutKey MakeFrameSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eAllGraphics),
        },
    };
    return key;
}

DescriptorSetLayoutKey MakeMaterialSetLayoutKey()
{
    DescriptorSetLayoutKey key;
    key.bindings = {
        DescriptorBinding{
            .binding = 0,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::UniformBuffer),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 1,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 2,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
        DescriptorBinding{
            .binding = 3,
            .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::CombinedImageSampler),
            .descriptor_count = 1,
            .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eFragment),
        },
    };
    return key;
}

PipelineKey MakePipelineKey(uint32_t pass_id,
                            uint32_t shader_id,
                            ave::resource::MeshRuntime const& mesh)
{
    PipelineKey key{};
    key.pass_id = pass_id;
    key.shader_id = shader_id;
    key.vertex_layout_id = VertexLayoutIdFromMesh(mesh);
    key.render_state_id = 1;
    key.layout_profile = 0;
    key.rt_format = 0;
    key.depth_format = 0;
    key.stencil_format = 0;
    key.sample_count = 1;
    key.viewport_width = 0;
    key.viewport_height = 0;
    return key;
}

constexpr uint32_t kShadowMapSize = 1024;

glm::mat4 BuildShadowViewProjection(PassExecutionView const& view, core::FrameData const* frame)
{
    glm::vec3 scene_center = frame ? frame->view.world_position : glm::vec3{0.0f};
    glm::vec3 min_bounds = scene_center;
    glm::vec3 max_bounds = scene_center;
    bool has_bounds = false;

    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }
        glm::vec3 const position = glm::vec3(renderable->world[3]);
        if (!has_bounds) {
            min_bounds = position;
            max_bounds = position;
            has_bounds = true;
        } else {
            min_bounds = glm::min(min_bounds, position);
            max_bounds = glm::max(max_bounds, position);
        }
    }

    if (has_bounds) {
        scene_center = (min_bounds + max_bounds) * 0.5f;
    }

    glm::vec3 light_direction{0.35f, -1.0f, 0.25f};
    for (auto const* light : view.lights) {
        if (!light || !light->cast_shadows) {
            continue;
        }
        if (light->type == "directional" || light->type.empty()) {
            light_direction = light->direction;
            break;
        }
    }

    if (glm::length(light_direction) < 0.0001f) {
        light_direction = glm::vec3{0.35f, -1.0f, 0.25f};
    }
    light_direction = glm::normalize(light_direction);

    glm::vec3 up = std::abs(glm::dot(light_direction, glm::vec3{0.0f, 1.0f, 0.0f})) > 0.95f
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};

    float radius = 20.0f;
    if (has_bounds) {
        glm::vec3 const extents = glm::abs(max_bounds - min_bounds);
        radius = std::max(std::max(extents.x, extents.y), std::max(extents.z, 10.0f)) * 0.8f;
    }

    glm::vec3 const eye = scene_center - light_direction * (radius * 2.5f);
    glm::mat4 const light_view = glm::lookAt(eye, scene_center, up);
    glm::mat4 const light_projection = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 6.0f);
    glm::mat4 shadow_view_projection = light_projection * light_view;
    shadow_view_projection[1][1] *= -1.0f;
    return shadow_view_projection;
}

void TransitionImageLayout(vk::CommandBuffer const& command_buffer,
                           vk::Image image,
                           vk::ImageAspectFlags aspect_mask,
                           vk::ImageLayout old_layout,
                           vk::ImageLayout new_layout,
                           vk::AccessFlags src_access_mask,
                           vk::AccessFlags dst_access_mask,
                           vk::PipelineStageFlags src_stage,
                           vk::PipelineStageFlags dst_stage)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstAccessMask = dst_access_mask;
    command_buffer.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

bool BeginShadowMapRendering(RenderPassContext const& context,
                             vkfw::VkTexture const& shadow_map,
                             uint32_t shadow_map_size,
                             vk::ClearDepthStencilValue const& clear_depth)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{} || !shadow_map.IsInitialized()) {
        return false;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;

    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo depth_attachment{};
            depth_attachment.imageView = shadow_map.View();
            depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            depth_attachment.clearValue.depthStencil = clear_depth;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 0;
            rendering_info.pDepthAttachment = &depth_attachment;

            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR depth_attachment{};
            depth_attachment.imageView = shadow_map.View();
            depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            depth_attachment.clearValue.depthStencil = clear_depth;

            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 0;
            rendering_info.pDepthAttachment = &depth_attachment;

            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    return false;
}

void EndShadowMapRendering(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            context.command_buffer.endRendering();
        } else {
            context.command_buffer.endRenderingKHR();
        }
    }
}

bool BeginSwapchainRendering(RenderPassContext const& context, vk::ClearValue const& clear_value, bool clear_color)
{
    if (context.vk == nullptr || context.swapchain == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return false;
    }

    auto const extent = context.swapchain->Extent();
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;

    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;

            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;

            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    if (context.compatibility_render_pass == vk::RenderPass{} ||
        context.compatibility_framebuffer == vk::Framebuffer{}) {
        return false;
    }

    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = context.compatibility_render_pass;
    render_pass_begin.framebuffer = context.compatibility_framebuffer;
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, extent};
    render_pass_begin.clearValueCount = 1;
    render_pass_begin.pClearValues = &clear_value;
    context.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    return true;
}

void EndSwapchainRendering(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            context.command_buffer.endRendering();
        } else {
            context.command_buffer.endRenderingKHR();
        }
    } else {
        context.command_buffer.endRenderPass();
    }
}

} // namespace

PassDataFilter DepthPrepass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::DepthPrepass;
    filter.opaque_only = true;
    return filter;
}

void DepthPrepass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: DepthPrepass");
    (void)context;
    (void)view;
}

PassDataFilter ShadowPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Shadow;
    filter.opaque_only = true;
    filter.shadow_casters_only = true;
    return filter;
}

void ShadowPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ShadowPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct FrameUbo {
        glm::mat4 view_projection{1.0f};
    };

    if (!has_vk) {
        return;
    }

    if (shadow_shader_id_ == 0) {
        shadow_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
        if (shadow_shader_id_ == 0) {
            __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "ShadowPass failed to load shadow shader");
            return;
        }
    }

    if (!shadow_map_.IsInitialized()) {
        if (!shadow_map_.Init(*context.vk, vkfw::TextureInfo{
                                               .width = kShadowMapSize,
                                               .height = kShadowMapSize,
                                               .mip_levels = 1,
                                               .format = vkfw::TextureFormat::D32_SFLOAT,
                                               .usage = static_cast<vkfw::TextureUsage>(
                                                   static_cast<uint32_t>(vkfw::TextureUsage::DepthStencilAttachment) |
                                                   static_cast<uint32_t>(vkfw::TextureUsage::Sampled)),
                                               .mipmap = false,
                                           })) {
            __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "ShadowPass failed to create shadow map");
            return;
        }
        shadow_map_initialized_ = false;
    }

    shadow_view_projection_ = BuildShadowViewProjection(view, context.frame);

    FrameUbo frame_ubo{};
    frame_ubo.view_projection = shadow_view_projection_;

    if (!frame_ubo_.IsInitialized()) {
        frame_ubo_.Init(*context.vk, vkfw::BufferInfo{
                                        .size = static_cast<uint32_t>(sizeof(FrameUbo)),
                                        .usage = vkfw::BufferUsage::Uniform,
                                        .mappable = true,
                                    });
    }
    frame_ubo_.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(FrameUbo)));

    if (frame_set_id_ == 0) {
        uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
        frame_set_id_ = desc_alloc.AllocateDescriptorSet(frame_layout_id);
    }
    if (frame_set_id_ != 0) {
        desc_alloc.UpdateUniformBuffer(frame_set_id_, 0, frame_ubo_.Handle(), 0, sizeof(FrameUbo));
    }

    vk::ImageLayout const old_layout = shadow_map_initialized_
        ? vk::ImageLayout::eShaderReadOnlyOptimal
        : vk::ImageLayout::eUndefined;
    TransitionImageLayout(context.command_buffer,
                          shadow_map_.Handle(),
                          vk::ImageAspectFlagBits::eDepth,
                          old_layout,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          {},
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                          vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eEarlyFragmentTests);

    vk::ClearDepthStencilValue clear_depth{};
    clear_depth.depth = 1.0f;
    clear_depth.stencil = 0;
    if (!BeginShadowMapRendering(context, shadow_map_, kShadowMapSize, clear_depth)) {
        __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "ShadowPass failed to begin shadow-map rendering");
        return;
    }

    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            continue;
        }

        PipelineKey key = MakePipelineKey(1, shadow_shader_id_, *mesh);
        key.layout_profile = 3;
        key.rt_format = 0;
        key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
        key.viewport_width = kShadowMapSize;
        key.viewport_height = kShadowMapSize;

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        if (frame_set_id_ != 0) {
            vk::DescriptorSet const frame_set = desc_alloc.GetHandle(frame_set_id_);
            if (frame_set) {
                context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                          pipeline->Layout(),
                                                          0,
                                                          1,
                                                          &frame_set,
                                                          0,
                                                          nullptr);
            }
        }

        vk::DeviceSize offset = 0;
        context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
        if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
            context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
            uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
            context.command_buffer.drawIndexed(index_count, 1, renderable->first_index, static_cast<int32_t>(renderable->first_vertex), 0);
        } else {
            uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
            context.command_buffer.draw(vertex_count, 1, renderable->first_vertex, 0);
        }
    }

    EndShadowMapRendering(context);
    TransitionImageLayout(context.command_buffer,
                          shadow_map_.Handle(),
                          vk::ImageAspectFlagBits::eDepth,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                          vk::AccessFlagBits::eShaderRead,
                          vk::PipelineStageFlagBits::eLateFragmentTests,
                          vk::PipelineStageFlagBits::eFragmentShader);
    shadow_map_initialized_ = true;
}

PassDataFilter PBRPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ForwardOpaque;
    filter.layer_mask = 0xFFFFFFFFu;
    return filter;
}

void PBRPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: PBRPass");

    if (context.resources == nullptr || context.pipelines == nullptr) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& mat_mgr = context.resources->GetMaterialManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    struct FrameUbo {
        glm::mat4 view_projection{1.0f};
    };

    struct MaterialUbo {
        glm::vec4 base_color{1.0f};
        glm::vec4 params{0.0f};
    };

    bool began_rendering = false;
    if (has_vk) {
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.03f;
        clear.color.float32[1] = 0.04f;
        clear.color.float32[2] = 0.06f;
        clear.color.float32[3] = 1.0f;
        began_rendering = BeginSwapchainRendering(context, clear, true);
        if (!began_rendering) {
            __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "PBRPass failed to begin rendering");
            return;
        }
    }

    if (has_vk) {
        FrameUbo frame_ubo{};
        if (context.frame != nullptr) {
            frame_ubo.view_projection = context.frame->view.view_projection;
        }

        if (!frame_ubo_.IsInitialized()) {
            frame_ubo_.Init(*context.vk, vkfw::BufferInfo{
                                            .size = static_cast<uint32_t>(sizeof(FrameUbo)),
                                            .usage = vkfw::BufferUsage::Uniform,
                                            .mappable = true,
                                        });
        }
        frame_ubo_.UpdateData(*context.vk, &frame_ubo, static_cast<uint32_t>(sizeof(FrameUbo)));

        if (frame_set_id_ == 0) {
            uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
            frame_set_id_ = desc_alloc.AllocateDescriptorSet(frame_layout_id);
        }
        if (frame_set_id_ != 0) {
            desc_alloc.UpdateUniformBuffer(frame_set_id_, 0, frame_ubo_.Handle(), 0, sizeof(FrameUbo));
        }
    }
    uint32_t renderable_index = 0;
    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            renderable_index++;
            continue;
        }

        // Apply GPU-based/CPU-fallback Frustum Culling
        if (renderable_index < g_culling_visibility.size() && g_culling_visibility[renderable_index] == 0) {
            __android_log_print(ANDROID_LOG_INFO, "CullingSystem", "  Skip draw call (culled): %s", renderable->debug_name.c_str());
            renderable_index++;
            continue;
        }
        __android_log_print(ANDROID_LOG_ERROR, "RenderVulkan", "frame_index: %llu", context.frame->frame_index);
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip material: %s", renderable->material_id.c_str());
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip mesh: %s", renderable->mesh_id.c_str());
            continue;
        }

        auto const* shader = material->shader_id != 0 ? shader_mgr.GetShader(material->shader_id) : nullptr;
        if (!shader) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  skip shader for material: %s", material->name.c_str());
            continue;
        }

        PipelineKey key = MakePipelineKey(0, shader->id, *mesh);
        key.layout_profile = 2;
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  pipeline create failed: %s", renderable->debug_name.c_str());
            continue;
        }

        if (!has_vk) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        auto& material_binding = material_bindings_[material->id];
        if (!material_binding.ubo.IsInitialized()) {
            material_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                       .size = static_cast<uint32_t>(sizeof(MaterialUbo)),
                                                       .usage = vkfw::BufferUsage::Uniform,
                                                       .mappable = true,
                                                   });
        }
        if (material_binding.descriptor_set_id == 0) {
            uint32_t const material_layout_id = desc_cache.GetOrCreateLayout(MakeMaterialSetLayoutKey());
            material_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(material_layout_id);
        }

        MaterialUbo material_ubo{};
        material_ubo.base_color = material->base_color;
        material_ubo.params = glm::vec4(material->metallic, material->roughness, 0.0f, 0.0f);
        material_binding.ubo.UpdateData(*context.vk, &material_ubo, static_cast<uint32_t>(sizeof(MaterialUbo)));

        if (material_binding.descriptor_set_id != 0) {
            desc_alloc.UpdateUniformBuffer(material_binding.descriptor_set_id,
                                           0,
                                           material_binding.ubo.Handle(),
                                           0,
                                           sizeof(MaterialUbo));

            vk::Sampler const sampler = GetCommonSampler(*context.vk);
            if (auto const* base_color_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->base_color_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              1,
                                              sampler,
                                              base_color_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* normal_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->normal_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              2,
                                              sampler,
                                              normal_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            if (auto const* mr_texture =
                    ResolveTextureOrFallback(*context.vk, texture_mgr, material->metallic_roughness_texture, fallback_white_texture_)) {
                desc_alloc.UpdateImageSampler(material_binding.descriptor_set_id,
                                              3,
                                              sampler,
                                              mr_texture->View(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        vk::DescriptorSet sets[2]{};
        uint32_t set_count = 0;
        if (frame_set_id_ != 0) {
            vk::DescriptorSet const frame_set = desc_alloc.GetHandle(frame_set_id_);
            if (frame_set) {
                sets[set_count++] = frame_set;
            }
        }
        if (material_binding.descriptor_set_id != 0) {
            vk::DescriptorSet const material_set = desc_alloc.GetHandle(material_binding.descriptor_set_id);
            if (material_set) {
                sets[set_count++] = material_set;
            }
        }
        if (set_count > 0) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      set_count,
                                                      sets,
                                                      0,
                                                      nullptr);
        }

        vk::DeviceSize offset = 0;
        context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
        if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
            context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
            uint32_t const index_count = renderable->index_count != 0 ? renderable->index_count : mesh->index_count;
            context.command_buffer.drawIndexed(index_count, 1, renderable->first_index, static_cast<int32_t>(renderable->first_vertex), 0);
        } else {
            uint32_t const vertex_count = renderable->vertex_count != 0 ? renderable->vertex_count : mesh->vertex_count;
            context.command_buffer.draw(vertex_count, 1, renderable->first_vertex, 0);
        }

        __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
    }

    if (began_rendering) {
        EndSwapchainRendering(context);
    }
}

PassDataFilter ComputePass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Compute;
    return filter;
}

void ComputePass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ComputePass");
    (void)view;
    uint32_t const object_count = static_cast<uint32_t>(view.renderables.size());
    
    // Resize culling visibility array to match current renderables count
    if (g_culling_visibility.size() != object_count) {
        g_culling_visibility.assign(object_count, 1u); // Default all visible
    }

    if (object_count == 0) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    uint64_t const frame_index = context.frame ? context.frame->frame_index : 0;
    uint32_t const buf_idx = static_cast<uint32_t>(frame_index % 2);

    if (has_vk) {
        // --- 1. Read Back Previous Results (from 2 frames ago, using this frame's buffer index) ---
        // Since we are double-buffering, and the fence for this frame-in-flight (buf_idx) has just been waited on,
        // the visibility_buffers_[buf_idx] is guaranteed to have finished GPU execution!
        if (frame_index >= 2 && visibility_buffers_[buf_idx].IsInitialized()) {
            uint32_t const* mapped_vis = static_cast<uint32_t const*>(visibility_buffers_[buf_idx].MappedData());
            if (mapped_vis != nullptr) {
                uint32_t const elements_to_copy = std::min(object_count, visibility_buffers_[buf_idx].Size() / (uint32_t)sizeof(uint32_t));
                for (uint32_t i = 0; i < elements_to_copy; ++i) {
                    g_culling_visibility[i] = mapped_vis[i];
                }
                // Any extra new objects are default visible
                for (uint32_t i = elements_to_copy; i < object_count; ++i) {
                    g_culling_visibility[i] = 1u;
                }
            }
        }
    }

    // --- 2. CPU Fallback / Culling Calculations ---
    // If not running on Vulkan, or during the first two frames where we don't have GPU readback yet,
    // we do a quick CPU culling pass to keep the framerate high and correct.
    if (!has_vk || frame_index < 2) {
        if (context.frame) {
            glm::mat4 const view_proj = context.frame->view.view_projection;
            glm::vec4 planes[6];
            planes[0] = glm::vec4(view_proj[0][3] + view_proj[0][0], view_proj[1][3] + view_proj[1][0], view_proj[2][3] + view_proj[2][0], view_proj[3][3] + view_proj[3][0]);
            planes[1] = glm::vec4(view_proj[0][3] - view_proj[0][0], view_proj[1][3] - view_proj[1][0], view_proj[2][3] - view_proj[2][0], view_proj[3][3] - view_proj[3][0]);
            planes[2] = glm::vec4(view_proj[0][3] + view_proj[0][1], view_proj[1][3] + view_proj[1][1], view_proj[2][3] + view_proj[2][1], view_proj[3][3] + view_proj[3][1]);
            planes[3] = glm::vec4(view_proj[0][3] - view_proj[0][1], view_proj[1][3] - view_proj[1][1], view_proj[2][3] - view_proj[2][1], view_proj[3][3] - view_proj[3][1]);
            planes[4] = glm::vec4(view_proj[0][3] + view_proj[0][2], view_proj[1][3] + view_proj[1][2], view_proj[2][3] + view_proj[2][2], view_proj[3][3] + view_proj[3][2]);
            planes[5] = glm::vec4(view_proj[0][3] - view_proj[0][2], view_proj[1][3] - view_proj[1][2], view_proj[2][3] - view_proj[2][2], view_proj[3][3] - view_proj[3][2]);
            for (int i = 0; i < 6; ++i) {
                float length = glm::length(glm::vec3(planes[i]));
                if (length > 0.0f) {
                    planes[i] /= length;
                }
            }

            for (uint32_t i = 0; i < object_count; ++i) {
                auto const* r = view.renderables[i];
                if (!r) continue;
                glm::vec3 center = glm::vec3(r->world[3]);
                float radius = 1.5f; // Bounding radius
                glm::vec3 min_bounds = center - glm::vec3(radius);
                glm::vec3 max_bounds = center + glm::vec3(radius);
                bool visible = true;
                for (int p = 0; p < 6; ++p) {
                    float px = planes[p].x > 0.0f ? max_bounds.x : min_bounds.x;
                    float py = planes[p].y > 0.0f ? max_bounds.y : min_bounds.y;
                    float pz = planes[p].z > 0.0f ? max_bounds.z : min_bounds.z;
                    float dist = planes[p].x * px + planes[p].y * py + planes[p].z * pz + planes[p].w;
                    if (dist < 0.0f) {
                        visible = false;
                        break;
                    }
                }
                g_culling_visibility[i] = visible ? 1u : 0u;
            }
        }
    }

    // --- 3. GPU Dispatch (Record to Command Buffer) ---
    if (has_vk) {
        // Load compute shader from assets if not loaded
        auto& shader_mgr = context.resources->GetShaderManager();
        if (g_culling_shader_id == 0) {
            g_culling_shader_id = shader_mgr.LoadComputeShader("compiled_shaders/culling.comp.spv");
        }

        // Initialize/resize buffer data
        struct InstanceData {
            glm::vec4 position_radius; // xyz = position, w = radius
        };
        std::vector<InstanceData> cpu_instances(object_count);
        for (uint32_t i = 0; i < object_count; ++i) {
            auto const* r = view.renderables[i];
            cpu_instances[i].position_radius = glm::vec4(r ? glm::vec3(r->world[3]) : glm::vec3(0.0f), 1.5f);
        }

        uint32_t const inst_size = object_count * sizeof(InstanceData);
        uint32_t const vis_size = object_count * sizeof(uint32_t);

        // Ensure buffers are initialized and large enough
        if (!instances_buffers_[buf_idx].IsInitialized() || instances_buffers_[buf_idx].Size() < inst_size) {
            if (instances_buffers_[buf_idx].IsInitialized()) {
                instances_buffers_[buf_idx].Shutdown(*context.vk);
            }
            instances_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                .size = inst_size,
                .usage = vkfw::BufferUsage::Storage,
                .mappable = true
            });
        }
        if (!visibility_buffers_[buf_idx].IsInitialized() || visibility_buffers_[buf_idx].Size() < vis_size) {
            if (visibility_buffers_[buf_idx].IsInitialized()) {
                visibility_buffers_[buf_idx].Shutdown(*context.vk);
            }
            visibility_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                .size = vis_size,
                .usage = vkfw::BufferUsage::Storage,
                .mappable = true
            });
        }

        // Write instances buffer
        instances_buffers_[buf_idx].UpdateData(*context.vk, cpu_instances.data(), inst_size);

        // Write descriptor set
        auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
        auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();

        if (descriptor_set_ids_[buf_idx] == 0) {
            DescriptorSetLayoutKey culling_set_key;
            culling_set_key.bindings = {
                DescriptorBinding{
                    .binding = 0,
                    .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                    .descriptor_count = 1,
                    .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                },
                DescriptorBinding{
                    .binding = 1,
                    .descriptor_type = static_cast<uint32_t>(vkfw::DescriptorType::StorageBuffer),
                    .descriptor_count = 1,
                    .stage_flags = static_cast<uint32_t>(vk::ShaderStageFlagBits::eCompute),
                }
            };
            uint32_t const set_layout_id = desc_cache.GetOrCreateLayout(culling_set_key);
            descriptor_set_ids_[buf_idx] = desc_alloc.AllocateDescriptorSet(set_layout_id);
        }

        // Update descriptors
        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 0, instances_buffers_[buf_idx].Handle(), 0, inst_size);
        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 1, visibility_buffers_[buf_idx].Handle(), 0, vis_size);

        // Get Compute Pipeline
        PipelineKey pipe_key;
        pipe_key.shader_id = g_culling_shader_id;
        pipe_key.layout_profile = 4;

        uint32_t const pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(pipe_key, context.compatibility_render_pass);
        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (pipeline) {
            vk::CommandBuffer cmd = context.command_buffer;

            // Bind compute pipeline
            cmd.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            // Bind descriptor set
            vk::DescriptorSet desc_set = desc_alloc.GetHandle(descriptor_set_ids_[buf_idx]);
            if (desc_set) {
                cmd.bindDescriptorSets(
                    pipeline->BindPoint(),
                    pipeline->Layout(),
                    0, 1, &desc_set, 0, nullptr
                );
            }

            // Push Constants (frustum planes + instance count)
            struct PushConstants {
                glm::vec4 planes[6];
                uint32_t total_instances;
            } pc{};
            
            if (context.frame) {
                glm::mat4 const view_proj = context.frame->view.view_projection;
                pc.planes[0] = glm::vec4(view_proj[0][3] + view_proj[0][0], view_proj[1][3] + view_proj[1][0], view_proj[2][3] + view_proj[2][0], view_proj[3][3] + view_proj[3][0]);
                pc.planes[1] = glm::vec4(view_proj[0][3] - view_proj[0][0], view_proj[1][3] - view_proj[1][0], view_proj[2][3] - view_proj[2][0], view_proj[3][3] - view_proj[3][0]);
                pc.planes[2] = glm::vec4(view_proj[0][3] + view_proj[0][1], view_proj[1][3] + view_proj[1][1], view_proj[2][3] + view_proj[2][1], view_proj[3][3] + view_proj[3][1]);
                pc.planes[3] = glm::vec4(view_proj[0][3] - view_proj[0][1], view_proj[1][3] - view_proj[1][1], view_proj[2][3] - view_proj[2][1], view_proj[3][3] - view_proj[3][1]);
                pc.planes[4] = glm::vec4(view_proj[0][3] + view_proj[0][2], view_proj[1][3] + view_proj[1][2], view_proj[2][3] + view_proj[2][2], view_proj[3][3] + view_proj[3][2]);
                pc.planes[5] = glm::vec4(view_proj[0][3] - view_proj[0][2], view_proj[1][3] - view_proj[1][2], view_proj[2][3] - view_proj[2][2], view_proj[3][3] - view_proj[3][2]);
                for (int i = 0; i < 6; ++i) {
                    float length = glm::length(glm::vec3(pc.planes[i]));
                    if (length > 0.0f) {
                        pc.planes[i] /= length;
                    }
                }
            }
            pc.total_instances = object_count;

            cmd.pushConstants(pipeline->Layout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(PushConstants), &pc);

            // Dispatch
            uint32_t const group_count = (object_count + 15) / 16;
            cmd.dispatch(group_count, 1, 1);

            // Insert pipeline barrier: transition Storage Buffer (shader write) to Host read access
            vk::BufferMemoryBarrier barrier{};
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = visibility_buffers_[buf_idx].Handle();
            barrier.offset = 0;
            barrier.size = vis_size;

            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eHost,
                vk::DependencyFlags{},
                nullptr,
                barrier,
                nullptr
            );
        }
    }

    // Log culling ratio
    uint32_t visible_count = 0;
    for (uint32_t i = 0; i < object_count; ++i) {
        if (g_culling_visibility[i] != 0) visible_count++;
    }
    __android_log_print(ANDROID_LOG_INFO, "CullingSystem", "GPU Culling: %u / %u visible (Ratio: %.2f%%)",
                        visible_count, object_count, (float)visible_count / (float)object_count * 100.0f);
}

PassDataFilter UIPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::UI;
    filter.layer_mask = core::ToMask(core::RenderLayer::UI);
    return filter;
}

void UIPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: UIPass");
    for (auto const* item : view.ui_items) {
        if (item) {
            __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "  ui: %s", item->debug_name.c_str());
        }
    }
    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    if (has_vk) {
        vk::ClearValue clear{};
        clear.color.float32[0] = 0.0f;
        clear.color.float32[1] = 0.0f;
        clear.color.float32[2] = 0.0f;
        clear.color.float32[3] = 0.0f;
        if (BeginSwapchainRendering(context, clear, false)) {
            EndSwapchainRendering(context);
        }
    }
}

PassDataFilter ToneMappingPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ToneMapping;
    return filter;
}

void ToneMappingPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    __android_log_print(ANDROID_LOG_INFO, "RenderVulkan", "Pass: ToneMappingPass");
    (void)context;
    (void)view;
}

} // namespace ave::render
