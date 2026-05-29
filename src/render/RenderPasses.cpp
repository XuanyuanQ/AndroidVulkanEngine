
#include "ave/render/RenderPasses.h"

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

static std::unique_ptr<vk::raii::RenderPass> g_compatibility_shadow_render_pass;
static std::unique_ptr<vk::raii::Framebuffer> g_compatibility_shadow_framebuffer;
static vk::ImageView g_last_shadow_image_view = {};

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

vk::Sampler GetShadowSampler(vkfw::VkContext& ctx)
{
    static std::unique_ptr<vk::raii::Sampler> sampler;
    if (!sampler) {
        vk::SamplerCreateInfo create_info{};
        create_info.magFilter = vk::Filter::eLinear;
        create_info.minFilter = vk::Filter::eLinear;
        create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        
        // 阴影贴图边缘外不要重复，使用 Clamp 边界模式
        create_info.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        create_info.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        create_info.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        
        // 边界颜色设为不透明白色，代表阴影图外永远是亮部
        create_info.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        
        create_info.maxLod = VK_LOD_CLAMP_NONE;

        // 【核心】开启硬件深度比较开关
        create_info.compareEnable = VK_FALSE; 
        create_info.compareOp = vk::CompareOp::eLessOrEqual; // 如果当前像素深度小于或等于阴影图里的深度，则视为可见

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


PipelineKey MakePipelineKey(uint32_t shader_id,
                            ave::resource::MeshRuntime const& mesh)
{
    PipelineKey key{};
    key.shader_id = shader_id;
    key.render_state_id = 1;
    key.layout_profile = PipelineLayoutProfile::Empty;
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
    bool has_light_direction = false;
    for (auto const* light : view.lights) {
        if (!light || !light->cast_shadows) {
            continue;
        }
        if (light->type == "directional" || light->type.empty()) {
            if (glm::length(light->direction) > 0.0001f) {
                light_direction = light->direction;
                has_light_direction = true;
                break;
            }
        } else {
            glm::vec3 const to_scene = scene_center - light->position;
            if (glm::length(to_scene) > 0.0001f) {
                light_direction = to_scene;
                has_light_direction = true;
                break;
            }
        }
    }

    if (!has_light_direction || glm::length(light_direction) < 0.0001f) {
        light_direction = glm::vec3{0.35f, -1.0f, 0.25f};
    }
    light_direction = glm::normalize(light_direction);

    glm::vec3 up = std::abs(glm::dot(light_direction, glm::vec3{0.0f, 1.0f, 0.0f})) > 0.95f
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};

    // float radius = 20.0f;
    float radius = 500.0f;
    if (has_bounds) {
        glm::vec3 const extents = glm::abs(max_bounds - min_bounds);
        radius = std::max(std::max(extents.x, extents.y), std::max(extents.z, 10.0f)) * 0.8f;
    }

    glm::vec3 const eye = scene_center - light_direction * (radius * 2.5f);
    glm::mat4 const light_view = glm::lookAtRH(eye, scene_center, up);
    glm::mat4 const light_projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1f, radius * 6.0f);
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
    // 1. 基础安全检查
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{} || !shadow_map.IsInitialized()) {
        return false;
    }
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    // ==========================================
    // 通道 A：支持动态渲染
    // ==========================================
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
    // ==========================================
    // 通道 B：不支持动态渲染（传统兼容路线 - RAII安全版）
    // ==========================================
    
    // 1. 现场定制：如果阴影专属 RenderPass 还没创建，使用 RAII 容器创建它
    if (!g_compatibility_shadow_render_pass) {
        vk::AttachmentDescription depth_attachment{};
        depth_attachment.format = shadow_map.Format(); // 自动匹配 D32Sfloat
        depth_attachment.samples = vk::SampleCountFlagBits::e1;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        // We transition the shadow map into DepthAttachmentOptimal before beginning
        // the fallback render pass, so the attachment description must agree with
        // the actual image layout at render-pass begin time.
        depth_attachment.initialLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        vk::AttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 0;
        depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;
        std::array<vk::SubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;
        vk::RenderPassCreateInfo render_pass_info{};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &depth_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        render_pass_info.pDependencies = dependencies.data();
        // 💡 使用 std::make_unique 创建 RAII 托管的 RenderPass
        g_compatibility_shadow_render_pass = std::make_unique<vk::raii::RenderPass>(context.vk->Device(), render_pass_info);
    }
    // 2. 现场定制：如果阴影贴图的 View 变更，调用 reset() 即可自动安全释放旧的 Framebuffer
    if (g_compatibility_shadow_framebuffer && g_last_shadow_image_view != shadow_map.View()) {
        g_compatibility_shadow_framebuffer.reset(); // 👈 这一行代替了手动的 destroyFramebuffer！
    }
    // 3. 现场定制：将专属的 shadow_map.View() 绑定到专属的 Framebuffer 上
    if (!g_compatibility_shadow_framebuffer) {
        g_last_shadow_image_view = shadow_map.View();
        vk::ImageView attachment = shadow_map.View();
        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.renderPass = **g_compatibility_shadow_render_pass; // 双星号解引用出原生 vk::RenderPass 句柄
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &attachment;
        framebuffer_info.width = shadow_map_size;
        framebuffer_info.height = shadow_map_size;
        framebuffer_info.layers = 1;
        // 💡 使用 std::make_unique 创建 RAII 托管的 Framebuffer
        g_compatibility_shadow_framebuffer = std::make_unique<vk::raii::Framebuffer>(context.vk->Device(), framebuffer_info);
    }
    // 4. 正式开启阴影专属的渲染通道
    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = **g_compatibility_shadow_render_pass;  // 双星号解引用
    render_pass_begin.framebuffer = **g_compatibility_shadow_framebuffer; // 双星号解引用
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
    vk::ClearValue clear_value{};
    clear_value.depthStencil = clear_depth;
    render_pass_begin.clearValueCount = 1;
    render_pass_begin.pClearValues = &clear_value;
    context.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    return true;
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
    } else {
        context.command_buffer.endRenderPass();
    }
}

bool BeginSwapchainRendering(RenderPassContext const& context, vk::ClearValue const& clear_value, bool clear_color, vkfw::VkTexture const* depth_texture = nullptr)
{
    if (context.vk == nullptr || context.swapchain == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return false;
    }

    auto const extent = context.swapchain->Extent();
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;

    if (context.vk->SupportsDynamicRendering()) {
        vk::ClearDepthStencilValue clear_depth{1.0f, 0};
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingAttachmentInfo depth_attachment{};
            if (depth_texture && depth_texture->IsInitialized()) {
                depth_attachment.imageView = depth_texture->View();
                depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
                depth_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
                depth_attachment.clearValue.depthStencil = clear_depth;
            }

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            if (depth_texture && depth_texture->IsInitialized()) {
                rendering_info.pDepthAttachment = &depth_attachment;
            }

            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingAttachmentInfoKHR depth_attachment{};
            if (depth_texture && depth_texture->IsInitialized()) {
                depth_attachment.imageView = depth_texture->View();
                depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
                depth_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
                depth_attachment.clearValue.depthStencil = clear_depth;
            }

            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            if (depth_texture && depth_texture->IsInitialized()) {
                rendering_info.pDepthAttachment = &depth_attachment;
            }

            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    vk::RenderPass compatibility_render_pass = clear_color
        ? context.compatibility_render_pass
        : context.compatibility_load_render_pass;
    vk::Framebuffer compatibility_framebuffer = clear_color
        ? context.compatibility_framebuffer
        : context.compatibility_load_framebuffer;

    if (compatibility_render_pass == vk::RenderPass{} ||
        compatibility_framebuffer == vk::Framebuffer{}) {
        return false;
    }

    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = compatibility_render_pass;
    render_pass_begin.framebuffer = compatibility_framebuffer;
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

struct UiDrawRange {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t texture_runtime_id = 0;
};

glm::vec3 ClampUiPosition(glm::vec2 const& position, float depth)
{
    return glm::vec3{
        std::clamp(position.x, -1.0f, 1.0f),
        std::clamp(position.y, -1.0f, 1.0f),
        std::clamp(depth, -1.0f, 1.0f),
    };
}

void AppendUiQuad(std::vector<ave::render::UiVertex>& vertices,
                  std::vector<uint32_t>& indices,
                  core::FrameUiData const& item,
                  uint32_t texture_index,
                  float aspect_ratio)
{
    glm::vec2 const half_size = item.size * 0.5f;
    glm::vec3 const center = ClampUiPosition(item.position, item.depth);

    uint32_t const base_vertex = static_cast<uint32_t>(vertices.size());

    auto make_vertex = [&](float x, float y, float u, float v) {
        ave::render::UiVertex vertex{};
        // aspect_ratio scales the local offset x-coordinate to prevent stretching on landscape/portrait screens
        vertex.position = glm::vec2{
            std::clamp(center.x + x / aspect_ratio, -1.0f, 1.0f),
            std::clamp(center.y + y, -1.0f, 1.0f),
        };
        vertex.uv = glm::vec2{u, v};
        vertex.color = item.color;
        vertex.texture_index = texture_index;
        return vertex;
    };

    vertices.push_back(make_vertex(-half_size.x, -half_size.y, 0.0f, 1.0f));
    vertices.push_back(make_vertex( half_size.x, -half_size.y, 1.0f, 1.0f));
    vertices.push_back(make_vertex( half_size.x,  half_size.y, 1.0f, 0.0f));
    vertices.push_back(make_vertex(-half_size.x,  half_size.y, 0.0f, 0.0f));

    indices.push_back(base_vertex + 0);
    indices.push_back(base_vertex + 1);
    indices.push_back(base_vertex + 2);
    indices.push_back(base_vertex + 0);
    indices.push_back(base_vertex + 2);
    indices.push_back(base_vertex + 3);
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
    LOGI( "RenderVulkan", "Pass: DepthPrepass");
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
    LOGI( "RenderVulkan", "Pass: ShadowPass");

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
        glm::mat4 shadow_view_projection{1.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    if (!has_vk) {
        return;
    }

    if (shadow_shader_id_ == 0) {
        shadow_shader_id_ = shader_mgr.LoadShader("compiled_shaders/shadow_depth");
        if (shadow_shader_id_ == 0) {
            LOGE( "RenderVulkan", "ShadowPass failed to load shadow shader");
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
            LOGE( "RenderVulkan", "ShadowPass failed to create shadow map");
            return;
        }
        shadow_map_initialized_ = false;
    }

    shadow_view_projection_ = BuildShadowViewProjection(view, context.frame);

    FrameUbo frame_ubo{};
    frame_ubo.shadow_view_projection = shadow_view_projection_;

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
        LOGE( "RenderVulkan", "ShadowPass failed to begin shadow-map rendering");
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

        PipelineKey key = MakePipelineKey(shadow_shader_id_, *mesh);
        key.layout_profile = PipelineLayoutProfile::Global_Set0_Only;
        key.rt_format = 0;
        key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
        key.viewport_width = kShadowMapSize;
        key.viewport_height = kShadowMapSize;

        vk::RenderPass active_render_pass = context.vk->SupportsDynamicRendering() 
            ? vk::RenderPass{} 
            : **g_compatibility_shadow_render_pass; // 👈 增加双星号解引用
        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, active_render_pass);
        if (pipeline_id == 0) {
            continue;
        }

        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (!pipeline) {
            continue;
        }

        context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

        ObjectPushConstants object_push{};
        object_push.world = renderable->world;
        context.command_buffer.pushConstants(pipeline->Layout(),
                                             vk::ShaderStageFlagBits::eVertex,
                                             0,
                                             sizeof(ObjectPushConstants),
                                             &object_push);

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
    // LOGI( "setRenderVulkan", "Shadow map initialized");
    context.current_shadow_map = &shadow_map_;
    context.shadow_view_projection = shadow_view_projection_;
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
    LOGI( "RenderVulkan", "Pass: PBRPass");

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
        glm::mat4 shadow_view_projection{1.0f};
    };

    struct MaterialUbo {
        glm::vec4 base_color{1.0f};
        glm::vec4 params{0.0f};
    };

    struct ObjectPushConstants {
        glm::mat4 world{1.0f};
    };

    bool began_rendering = false;
    if (has_vk) {
        uint32_t const width = context.swapchain->Extent().width;
        uint32_t const height = context.swapchain->Extent().height;

        if (depth_stencil_.IsInitialized()) {
            auto const extent = depth_stencil_.Extent();
            if (extent.width != width || extent.height != height) {
                depth_stencil_.Shutdown(*context.vk);
            }
        }

        if (!depth_stencil_.IsInitialized()) {
            if (!depth_stencil_.Init(*context.vk, vkfw::TextureInfo{
                                                   .width = width,
                                                   .height = height,
                                                   .mip_levels = 1,
                                                   .format = vkfw::TextureFormat::D32_SFLOAT,
                                                   .usage = vkfw::TextureUsage::DepthStencilAttachment,
                                                   .mipmap = false,
                                               })) {
                LOGE( "RenderVulkan", "PBRPass failed to create depth stencil texture");
                return;
            }
        }

        TransitionImageLayout(context.command_buffer,
                              depth_stencil_.Handle(),
                              vk::ImageAspectFlagBits::eDepth,
                              vk::ImageLayout::eUndefined,
                              vk::ImageLayout::eDepthAttachmentOptimal,
                              {},
                              vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead,
                              vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eEarlyFragmentTests);

        vk::ClearValue clear{};
        clear.color.float32[0] = 1.0f;
        clear.color.float32[1] = 1.0f;
        clear.color.float32[2] = 1.0f;
        clear.color.float32[3] = 1.0f;
        began_rendering = BeginSwapchainRendering(context, clear, true, &depth_stencil_);
        if (!began_rendering) {
            LOGE( "RenderVulkan", "PBRPass failed to begin rendering");
            return;
        }
    }

    if (has_vk) {
        FrameUbo frame_ubo{};
        if (context.frame != nullptr) {
            // frame_ubo.view_projection = context.shadow_view_projection;
            frame_ubo.view_projection = context.frame->view.view_projection;
            frame_ubo.shadow_view_projection = context.shadow_view_projection;  
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
            if(context.current_shadow_map) {
            vk::Sampler sampler = GetShadowSampler(*context.vk);
            desc_alloc.UpdateImageSampler(frame_set_id_, 1, sampler, context.current_shadow_map->View(), vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

    }
    uint32_t renderable_index = 0;
    for (auto const* renderable : view.renderables) {
        if (!has_vk) {
            continue;
        }
        if (!renderable) {
            renderable_index++;
            continue;
        }

        // Apply GPU-based/CPU-fallback Frustum Culling
        if (renderable_index < g_culling_visibility.size() && g_culling_visibility[renderable_index] == 0) {
            LOGI( "CullingSystem", "  Skip draw call (culled): %s", renderable->debug_name.c_str());
            renderable_index++;
            continue;
        }
        auto const* material = renderable->material_handle != 0
            ? mat_mgr.GetMaterial(renderable->material_handle)
            : mat_mgr.GetMaterialByName(renderable->material_id);
        if (!material) {
            if (fallback_material_id_ == 0) {
                uint32_t fallback_shader_id = shader_mgr.LoadShader("compiled_shaders/solid_triangle");
                if (fallback_shader_id != 0) {
                    fallback_material_id_ = mat_mgr.CreateMaterial("__fallback/default_material__", fallback_shader_id);
                    mat_mgr.SetBaseColor(fallback_material_id_, glm::vec4{0.85f, 0.85f, 0.88f, 1.0f});
                }
            }
            if (fallback_material_id_ != 0) {
                material = mat_mgr.GetMaterial(fallback_material_id_);
            }
        }
        if (!material) {
            LOGI( "RenderVulkan", "  skip material: %s", renderable->material_id.c_str());
            continue;
        }

        auto const* mesh = renderable->mesh_handle != 0
            ? mesh_mgr.GetMesh(renderable->mesh_handle)
            : mesh_mgr.GetMeshByPath(renderable->mesh_id);
        if (!mesh) {
            LOGI( "RenderVulkan", "  skip mesh: %s", renderable->mesh_id.c_str());
            continue;
        }

        auto const* shader = material->shader_id != 0 ? shader_mgr.GetShader(material->shader_id) : nullptr;
        if (!shader) {
            LOGI( "RenderVulkan", "  skip shader for material: %s", material->name.c_str());
            continue;
        }

        PipelineKey key = MakePipelineKey(shader->id, *mesh);
        key.layout_profile = PipelineLayoutProfile::Material_Set0_Set1;
        if (has_vk) {
            key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
            key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
            key.viewport_width = context.swapchain->Extent().width;
            key.viewport_height = context.swapchain->Extent().height;
        }

        uint32_t const pipeline_id =
            context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, context.compatibility_render_pass);
        if (pipeline_id == 0) {
            LOGI( "RenderVulkan", "  pipeline create failed: %s", renderable->debug_name.c_str());
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
        material_ubo.base_color = renderable->has_color_override ? renderable->color_override : material->base_color;
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

        ObjectPushConstants object_push{};
        object_push.world = renderable->world;
        context.command_buffer.pushConstants(pipeline->Layout(),
                                             vk::ShaderStageFlagBits::eVertex,
                                             0,
                                             sizeof(ObjectPushConstants),
                                             &object_push);

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

        LOGI( "RenderVulkan", "  draw: %s", renderable->debug_name.c_str());
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
    LOGI( "RenderVulkan", "Pass: ComputePass");
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
        pipe_key.layout_profile = PipelineLayoutProfile::ComputeCulling_Set0_Only;

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
    LOGI( "CullingSystem", "GPU Culling: %u / %u visible (Ratio: %.2f%%)",
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
    LOGI( "RenderVulkan", "Pass: UIPass");
    if (view.ui_items.empty()) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr &&
        context.swapchain != nullptr &&
        context.command_buffer != vk::CommandBuffer{} &&
        context.pipelines != nullptr &&
        context.resources != nullptr;

    uint64_t const frame_index = context.frame ? context.frame->frame_index : 0;
    uint32_t const buf_idx = static_cast<uint32_t>(frame_index % 2);

    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    std::vector<ave::render::UiVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(view.ui_items.size() * 4);
    indices.reserve(view.ui_items.size() * 6);

    // aspect ratio correction based on current swapchain extent (uses height/width for Android pre-rotation)
    float const width = has_vk ? static_cast<float>(context.swapchain->Extent().width) : 1080.0f;
    float const height = has_vk ? static_cast<float>(context.swapchain->Extent().height) : 1920.0f;
    float const aspect_ratio = (width > 0.0f) ? (height / width) : (9.0f / 16.0f);

    // 1. Gather all unique textures used this frame (max 15 slots, index 1 to 15)
    std::vector<std::string> unique_texture_paths;
    if (has_vk) {
        for (auto const* item : view.ui_items) {
            if (!item || !item->visible || item->texture_id.empty()) {
                continue;
            }
            if (std::find(unique_texture_paths.begin(), unique_texture_paths.end(), item->texture_id) == unique_texture_paths.end()) {
                if (unique_texture_paths.size() < 15) {
                    unique_texture_paths.push_back(item->texture_id);
                }
            }
        }
    }

    // Load or resolve texture runtime IDs
    std::vector<uint32_t> texture_runtime_ids(16, 0); // 16 slots, default to 0 (fallback white)
    for (size_t i = 0; i < unique_texture_paths.size(); ++i) {
        std::string const& path = unique_texture_paths[i];
        uint32_t runtime_id = 0;
        if (auto const* runtime = texture_mgr.GetTextureByPath(path)) {
            runtime_id = runtime->id;
        } else {
            runtime_id = texture_mgr.LoadTexture(path);
        }
        texture_runtime_ids[i + 1] = runtime_id;
    }

    // 2. Generate geometry
    for (auto const* item : view.ui_items) {
        if (!item || !item->visible) {
            continue;
        }
        LOGI( "RenderVulkan", "  ui: %s", item->debug_name.c_str());
        
        uint32_t texture_index = 0; // Default to fallback white slot
        if (!item->texture_id.empty()) {
            auto it = std::find(unique_texture_paths.begin(), unique_texture_paths.end(), item->texture_id);
            if (it != unique_texture_paths.end()) {
                texture_index = static_cast<uint32_t>(1 + std::distance(unique_texture_paths.begin(), it));
            }
        }
        
        AppendUiQuad(vertices, indices, *item, texture_index, aspect_ratio);
    }

    if (!has_vk || vertices.empty() || indices.empty()) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (ui_shader_id_ == 0) {
        ui_shader_id_ = shader_mgr.LoadShader("compiled_shaders/ui_textured");
        if (ui_shader_id_ == 0) {
            LOGE( "RenderVulkan", "UIPass failed to load ui_textured shader");
            return;
        }
    }

    uint32_t const vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(ave::render::UiVertex));
    uint32_t const index_bytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

    // Double buffering / Ring Buffering: manage ui_vertex_buffers_ and ui_index_buffers_ per buf_idx
    if (!ui_vertex_buffers_[buf_idx].IsInitialized() || ui_vertex_buffers_[buf_idx].Size() < vertex_bytes) {
        if (ui_vertex_buffers_[buf_idx].IsInitialized()) {
            ui_vertex_buffers_[buf_idx].Shutdown(*context.vk);
        }
        if (!ui_vertex_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                                                     .size = vertex_bytes,
                                                     .usage = vkfw::BufferUsage::Vertex,
                                                     .mappable = true,
                                                 })) {
            LOGE( "RenderVulkan", "UIPass failed to create vertex buffer");
            return;
        }
    }
    if (!ui_index_buffers_[buf_idx].IsInitialized() || ui_index_buffers_[buf_idx].Size() < index_bytes) {
        if (ui_index_buffers_[buf_idx].IsInitialized()) {
            ui_index_buffers_[buf_idx].Shutdown(*context.vk);
        }
        if (!ui_index_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                                                    .size = index_bytes,
                                                    .usage = vkfw::BufferUsage::Index,
                                                    .mappable = true,
                                                })) {
            LOGE( "RenderVulkan", "UIPass failed to create index buffer");
            return;
        }
    }

    ui_vertex_buffers_[buf_idx].UpdateData(*context.vk, vertices.data(), vertex_bytes);
    ui_index_buffers_[buf_idx].UpdateData(*context.vk, indices.data(), index_bytes);

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 0.0f;
    if (!BeginSwapchainRendering(context, clear, false)) {
        LOGE( "RenderVulkan", "UIPass failed to begin rendering");
        return;
    }

    ave::resource::MeshRuntime ui_mesh{};
    ui_mesh.vertex_stride = sizeof(ave::render::UiVertex);

    PipelineKey key = MakePipelineKey(ui_shader_id_, ui_mesh);
    key.vertex_layout_id = 2; // custom lightweight UiVertex format
    key.layout_profile = PipelineLayoutProfile::Texture_Set0_Only;
    key.render_state_id = 2;
    key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
    key.viewport_width = context.swapchain->Extent().width;
    key.viewport_height = context.swapchain->Extent().height;

    vk::RenderPass const ui_compatibility_render_pass =
        context.compatibility_load_render_pass != vk::RenderPass{}
            ? context.compatibility_load_render_pass
            : context.compatibility_render_pass;
    uint32_t const pipeline_id =
        context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, ui_compatibility_render_pass);
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        EndSwapchainRendering(context);
        LOGE( "RenderVulkan", "UIPass failed to create pipeline");
        return;
    }

    context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

    vk::DeviceSize offset = 0;
    context.command_buffer.bindVertexBuffers(0, ui_vertex_buffers_[buf_idx].Handle(), offset);
    context.command_buffer.bindIndexBuffer(ui_index_buffers_[buf_idx].Handle(), 0, vk::IndexType::eUint32);

    // 3. Use the same texture set layout as the UI pipeline layout.
    uint32_t const texture_layout_id = desc_cache.GetOrCreateLayout(MakeTextureSetLayoutKey());
    vk::Sampler const sampler = GetCommonSampler(*context.vk);
    EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);

    // Allocate double-buffered descriptor set
    uint32_t descriptor_set_id = texture_descriptor_sets_[buf_idx];
    if (descriptor_set_id == 0) {
        descriptor_set_id = desc_alloc.AllocateDescriptorSet(texture_layout_id);
        texture_descriptor_sets_[buf_idx] = descriptor_set_id;
    }

    if (descriptor_set_id != 0) {
        // Write slot 0 (fallback white)
        desc_alloc.UpdateImageSamplerArray(descriptor_set_id,
                                           0,
                                           0,
                                           sampler,
                                           fallback_white_texture_.View(),
                                           vk::ImageLayout::eShaderReadOnlyOptimal);
        // Write slots 1 to 15
        for (uint32_t slot = 1; slot < 16; ++slot) {
            uint32_t const runtime_id = texture_runtime_ids[slot];
            vkfw::VkTexture const* texture = ResolveTextureOrFallback(*context.vk, texture_mgr, runtime_id, fallback_white_texture_);
            if (texture != nullptr) {
                desc_alloc.UpdateImageSamplerArray(descriptor_set_id,
                                                   0,
                                                   slot,
                                                   sampler,
                                                   texture->View(),
                                                   vk::ImageLayout::eShaderReadOnlyOptimal);
            }
        }

        vk::DescriptorSet const set = desc_alloc.GetHandle(descriptor_set_id);
        if (set) {
            context.command_buffer.bindDescriptorSets(pipeline->BindPoint(),
                                                      pipeline->Layout(),
                                                      0,
                                                      1,
                                                      &set,
                                                      0,
                                                      nullptr);
        }
    }

    // 4. DRAW EVERYTHING IN ONE SINGLE BEAUTIFUL CALL!
    context.command_buffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    EndSwapchainRendering(context);
}

PassDataFilter ToneMappingPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ToneMapping;
    return filter;
}

void ToneMappingPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    LOGI( "RenderVulkan", "Pass: ToneMappingPass");
    (void)context;
    (void)view;
}

} // namespace ave::render
