#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {
namespace {

struct VolumetricLightUbo {
    glm::mat4 inverse_view_projection{1.0f};
    glm::mat4 shadow_view_projection{1.0f};
    glm::vec4 camera_position_near{0.0f, 0.0f, 0.0f, 0.1f};
    glm::vec4 light_position_range{0.0f, 6.0f, 6.0f, 20.0f};
    glm::vec4 light_direction_type{0.0f, -1.0f, 0.0f, 1.0f};
    glm::vec4 light_screen_density{0.5f, 0.5f, 0.55f, 1.0f};
    glm::vec4 light_color_intensity{1.0f, 0.92f, 0.72f, 0.45f};
    glm::vec4 params{0.88f, 0.18f, 0.75f, 0.0f};
};

core::FrameLightData const* FindVolumetricLight(PassExecutionView const& view)
{
    for (auto const* light : view.lights) {
        if (light != nullptr && light->volumetric) {
            return light;
        }
    }
    return nullptr;
}

void LogVolumetricStatusOnce(char const* message)
{
    static std::vector<char const*> logged_messages;
    if (std::find(logged_messages.begin(), logged_messages.end(), message) == logged_messages.end()) {
        logged_messages.push_back(message);
        LOGI("%s", message);
    }
}

glm::vec2 ProjectLightToScreen(core::FrameViewData const& frame_view, core::FrameLightData const& light)
{
    glm::vec4 clip{0.0f};
    if (light.type == "directional") {
        glm::vec3 const dir = glm::length(light.direction) > 0.001f
            ? glm::normalize(light.direction)
            : glm::vec3{0.0f, -1.0f, 0.0f};
        glm::vec3 const pseudo_position = frame_view.world_position - dir * 80.0f;
        clip = frame_view.view_projection * glm::vec4{pseudo_position, 1.0f};
    } else {
        clip = frame_view.view_projection * glm::vec4{light.position, 1.0f};
    }

    if (clip.w <= 0.0001f) {
        return glm::vec2{0.5f, 0.08f};
    }
    glm::vec2 ndc = glm::vec2{clip.x, clip.y} / clip.w;
    ndc = glm::clamp(ndc, glm::vec2{-1.25f, -1.25f}, glm::vec2{1.25f, 1.25f});
    return ndc * 0.5f + glm::vec2{0.5f, 0.5f};
}

void AppendFullscreenQuad(std::vector<UiVertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices = {
        UiVertex{glm::vec2{-1.0f, -1.0f}, glm::vec2{0.0f, 1.0f}, glm::vec4{1.0f}, 0},
        UiVertex{glm::vec2{ 1.0f, -1.0f}, glm::vec2{1.0f, 1.0f}, glm::vec4{1.0f}, 0},
        UiVertex{glm::vec2{ 1.0f,  1.0f}, glm::vec2{1.0f, 0.0f}, glm::vec4{1.0f}, 0},
        UiVertex{glm::vec2{-1.0f,  1.0f}, glm::vec2{0.0f, 0.0f}, glm::vec4{1.0f}, 0},
    };
    indices = {0, 1, 2, 0, 2, 3};
}

} // namespace

void VolumetricLightPass::Reset(vkfw::VkContext* ctx)
{
    shader_id_ = 0;
    if (ctx != nullptr) {
        for (auto& binding : frame_bindings_) {
            if (binding.ubo.IsInitialized()) {
                binding.ubo.Shutdown(*ctx);
            }
        }
        for (auto& buffer : vertex_buffers_) {
            if (buffer.IsInitialized()) {
                buffer.Shutdown(*ctx);
            }
        }
        for (auto& buffer : index_buffers_) {
            if (buffer.IsInitialized()) {
                buffer.Shutdown(*ctx);
            }
        }
    }
    frame_bindings_.clear();
    vertex_buffers_.clear();
    index_buffers_.clear();
}

PassDataFilter VolumetricLightPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::VolumetricLight;
    filter.layer_mask = 0xFFFFFFFFu;
    return filter;
}

void VolumetricLightPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    LogVolumetricStatusOnce("VolumetricLightPass entered");
    if (context.vk == nullptr ||
        context.resources == nullptr ||
        context.pipelines == nullptr ||
        context.frame == nullptr ||
        context.command_buffer == vk::CommandBuffer{} ||
        !context.color_target.IsValid()) {
        LogVolumetricStatusOnce("VolumetricLightPass skipped: invalid render context");
        return;
    }
    if (!context.vk->SupportsDynamicRendering()) {
        LogVolumetricStatusOnce("VolumetricLightPass skipped: dynamic rendering is required for depth sampling in this path");
        return;
    }

    core::FrameLightData const* light = FindVolumetricLight(view);
    if (light == nullptr) {
        LogVolumetricStatusOnce("VolumetricLightPass skipped: no light with volumetric=true");
        return;
    }
    LogVolumetricStatusOnce("VolumetricLightPass active: depth/shadow raymarch enabled");

    core::FrameViewData const* frame_view = CurrentFrameView(context);
    if (frame_view == nullptr) {
        return;
    }

    uint32_t const image_count = context.frame_resource_count != 0 ? context.frame_resource_count : 1u;
    uint32_t const image_index = context.frame_resource_index;
    if (image_count == 0 || image_index >= image_count) {
        return;
    }
    if (frame_bindings_.size() != image_count) {
        frame_bindings_.resize(image_count);
    }
    if (vertex_buffers_.size() != image_count) {
        vertex_buffers_.resize(image_count);
    }
    if (index_buffers_.size() != image_count) {
        index_buffers_.resize(image_count);
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    vkfw::VkTexture* shadow_map = context.frame_graph_resources != nullptr
        ? context.frame_graph_resources->GetTexture(FrameGraphResourceRegistry::ShadowMap)
        : nullptr;
    vkfw::VkTexture* depth_texture = context.depth_target.texture;
    if (shadow_map == nullptr || !shadow_map->IsInitialized() ||
        depth_texture == nullptr || !depth_texture->IsInitialized()) {
        LogVolumetricStatusOnce("VolumetricLightPass skipped: missing shadow map or depth texture");
        return;
    }

    if (shader_id_ == 0) {
        shader_id_ = shader_mgr.LoadShader("compiled_shaders/volumetric_light");
        if (shader_id_ == 0) {
            LOGE("VolumetricLightPass failed to load volumetric_light shader");
            return;
        }
    }

    std::vector<UiVertex> vertices;
    std::vector<uint32_t> indices;
    AppendFullscreenQuad(vertices, indices);

    uint32_t const vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(UiVertex));
    uint32_t const index_bytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    auto& vertex_buffer = vertex_buffers_[image_index];
    auto& index_buffer = index_buffers_[image_index];
    if (!vertex_buffer.IsInitialized() || vertex_buffer.Size() < vertex_bytes) {
        if (vertex_buffer.IsInitialized()) {
            vertex_buffer.Shutdown(*context.vk);
        }
        if (!vertex_buffer.Init(*context.vk, vkfw::BufferInfo{
                                               .size = vertex_bytes,
                                               .usage = vkfw::BufferUsage::Vertex,
                                               .mappable = true,
                                           })) {
            LOGE("VolumetricLightPass failed to create vertex buffer");
            return;
        }
    }
    if (!index_buffer.IsInitialized() || index_buffer.Size() < index_bytes) {
        if (index_buffer.IsInitialized()) {
            index_buffer.Shutdown(*context.vk);
        }
        if (!index_buffer.Init(*context.vk, vkfw::BufferInfo{
                                              .size = index_bytes,
                                              .usage = vkfw::BufferUsage::Index,
                                              .mappable = true,
                                          })) {
            LOGE("VolumetricLightPass failed to create index buffer");
            return;
        }
    }
    vertex_buffer.UpdateData(*context.vk, vertices.data(), vertex_bytes);
    index_buffer.UpdateData(*context.vk, indices.data(), index_bytes);

    auto& frame_binding = frame_bindings_[image_index];
    if (!frame_binding.ubo.IsInitialized()) {
        frame_binding.ubo.Init(*context.vk, vkfw::BufferInfo{
                                                .size = static_cast<uint32_t>(sizeof(VolumetricLightUbo)),
                                                .usage = vkfw::BufferUsage::Uniform,
                                                .mappable = true,
                                            });
    }

    glm::vec2 const light_screen = ProjectLightToScreen(*frame_view, *light);
    VolumetricLightUbo ubo{};
    ubo.inverse_view_projection = glm::inverse(frame_view->view_projection);
    if (context.frame_graph_resources != nullptr) {
        ubo.shadow_view_projection = context.frame_graph_resources->GetMatrix(
            FrameGraphResourceRegistry::ShadowViewProjection,
            glm::mat4{1.0f});
    }
    ubo.camera_position_near = glm::vec4{frame_view->world_position, frame_view->near_plane};
    ubo.light_position_range = glm::vec4{light->position, light->range};
    ubo.light_direction_type = glm::vec4{
        glm::length(light->direction) > 0.001f ? glm::normalize(light->direction) : glm::vec3{0.0f, -1.0f, 0.0f},
        light->type == "directional" ? 0.0f : 1.0f,
    };
    ubo.light_screen_density = glm::vec4{
        light_screen,
        std::max(light->volumetric_density, 0.001f),
        1.0f,
    };
    ubo.light_color_intensity = glm::vec4{
        light->color,
        std::clamp(light->volumetric_intensity * light->intensity * 0.15f, 0.0f, 2.2f),
    };
    ubo.params = glm::vec4{
        std::clamp(light->volumetric_decay, 0.65f, 0.98f),
        0.70f,
        1.25f,
        0.0f,
    };
    frame_binding.ubo.UpdateData(*context.vk, &ubo, static_cast<uint32_t>(sizeof(VolumetricLightUbo)));

    if (frame_binding.descriptor_set_id == 0) {
        uint32_t const frame_layout_id = desc_cache.GetOrCreateLayout(MakeFrameSetLayoutKey());
        frame_binding.descriptor_set_id = desc_alloc.AllocateDescriptorSet(frame_layout_id);
    }
    if (frame_binding.descriptor_set_id == 0) {
        return;
    }
    desc_alloc.UpdateUniformBuffer(frame_binding.descriptor_set_id, 0, frame_binding.ubo.Handle(), 0, sizeof(VolumetricLightUbo));
    desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id,
                                  1,
                                  GetShadowSampler(*context.vk),
                                  shadow_map->View(),
                                  vk::ImageLayout::eShaderReadOnlyOptimal);
    desc_alloc.UpdateImageSampler(frame_binding.descriptor_set_id,
                                  2,
                                  GetShadowSampler(*context.vk),
                                  depth_texture->View(),
                                  vk::ImageLayout::eDepthReadOnlyOptimal);

    ave::resource::MeshRuntime mesh{};
    mesh.vertex_stride = sizeof(UiVertex);
    PipelineKey key = MakePipelineKey(shader_id_, mesh);
    key.vertex_layout_id = 2;
    key.layout_profile = PipelineLayoutProfile::Global_Set0_Only;
    key.render_state_id = 4;
    key.rt_format = static_cast<uint32_t>(context.color_target.format);
    key.viewport_width = context.color_target.extent.width;
    key.viewport_height = context.color_target.extent.height;

    vk::RenderPass const compatibility_render_pass =
        context.color_target.compatibility_load_render_pass != vk::RenderPass{}
            ? context.color_target.compatibility_load_render_pass
            : context.color_target.compatibility_render_pass;
    uint32_t const pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, compatibility_render_pass);
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        LOGE("VolumetricLightPass failed to create pipeline");
        return;
    }

    TransitionImageLayout(context.command_buffer,
                          depth_texture->Handle(),
                          vk::ImageAspectFlagBits::eDepth,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          vk::ImageLayout::eDepthReadOnlyOptimal,
                          vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                          vk::AccessFlagBits::eShaderRead,
                          vk::PipelineStageFlagBits::eLateFragmentTests,
                          vk::PipelineStageFlagBits::eFragmentShader);

    auto restore_depth_layout = [&]() {
        TransitionImageLayout(context.command_buffer,
                              depth_texture->Handle(),
                              vk::ImageAspectFlagBits::eDepth,
                              vk::ImageLayout::eDepthReadOnlyOptimal,
                              vk::ImageLayout::eDepthAttachmentOptimal,
                              vk::AccessFlagBits::eShaderRead,
                              vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              vk::PipelineStageFlagBits::eEarlyFragmentTests);
    };

    vk::ClearValue clear{};
    clear.color.float32[3] = 0.0f;
    if (!BeginRenderTargetRendering(context, clear, false)) {
        restore_depth_layout();
        LOGE("VolumetricLightPass failed to begin rendering");
        return;
    }

    vk::DescriptorSet const set = desc_alloc.GetHandle(frame_binding.descriptor_set_id);
    context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());
    if (set) {
        context.command_buffer.bindDescriptorSets(pipeline->BindPoint(), pipeline->Layout(), 0, 1, &set, 0, nullptr);
    }
    vk::DeviceSize offset = 0;
    context.command_buffer.bindVertexBuffers(0, vertex_buffer.Handle(), offset);
    context.command_buffer.bindIndexBuffer(index_buffer.Handle(), 0, vk::IndexType::eUint32);
    context.command_buffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    EndSwapchainRendering(context);
    restore_depth_layout();
}

} // namespace ave::render
