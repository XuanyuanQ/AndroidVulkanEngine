#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {

PassDataFilter SkyboxPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::None;
    filter.layer_mask = 0u;
    return filter;
}

void SkyboxPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    (void)view;
    LOGI("SkyboxPass enter");
    if (context.resources == nullptr || context.pipelines == nullptr) {
        LOGW("SkyboxPass missing resources or pipelines");
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};
    if (!has_vk) {
        LOGW("SkyboxPass missing Vulkan context");
        return;
    }

    auto& mesh_mgr = context.resources->GetMeshManager();
    auto& shader_mgr = context.resources->GetShaderManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();

    if (skybox_shader_id_ == 0) {
        skybox_shader_id_ = shader_mgr.LoadShader("compiled_shaders/skybox");
        if (skybox_shader_id_ == 0) {
            LOGE("SkyboxPass failed to load skybox shader");
            return;
        }
    }
    if (skybox_mesh_id_ == 0) {
        skybox_mesh_id_ = mesh_mgr.LoadMesh("Cube");
        if (skybox_mesh_id_ == 0) {
            LOGE("SkyboxPass failed to load cube mesh for skybox");
            return;
        }
    }

    auto const* mesh = mesh_mgr.GetMesh(skybox_mesh_id_);
    auto const* shader = shader_mgr.GetShader(skybox_shader_id_);
    if (!mesh || !shader) {
        LOGW("SkyboxPass missing mesh or shader");
        return;
    }

    EnsureSharedEnvironmentMaps(*context.vk,
                                context.resources,
                                context.frame != nullptr ? context.frame->environment.clear_color
                                                         : glm::vec4{0.03f, 0.04f, 0.06f, 1.0f},
                                context.frame != nullptr ? context.frame->environment.ambient_color
                                                         : glm::vec3{0.04f, 0.04f, 0.045f});

    vk::ClearValue clear{};
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 1.0f;

    if (!BeginSwapchainRendering(context, clear, false, context.current_depth_texture, false)) {
        LOGE("SkyboxPass failed to begin rendering");
        return;
    }
    LOGI("SkyboxPass begin rendering ok");

    FrameUbo frame_ubo{};
    if (context.frame != nullptr) {
        frame_ubo.view_projection = context.frame->view.view_projection;
        frame_ubo.shadow_view_projection = context.shadow_view_projection;
        frame_ubo.camera_position = glm::vec4(context.frame->view.world_position, 1.0f);
        frame_ubo.ambient_color = glm::vec4(context.frame->environment.ambient_color, 1.0f);
        frame_ubo.clear_color = context.frame->environment.clear_color;
        frame_ubo.view = context.frame->view.view;
        frame_ubo.projection = context.frame->view.projection;
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
        vk::Sampler const sampler = GetCommonSampler(*context.vk);
        if (g_shared_environment_maps.environment_cubemap.IsInitialized()) {
            desc_alloc.UpdateImageSampler(frame_set_id_,
                                          2,
                                          sampler,
                                          g_shared_environment_maps.environment_cubemap.View(),
                                          vk::ImageLayout::eShaderReadOnlyOptimal);
        }
    }

    PipelineKey key = MakePipelineKey(shader->id, *mesh);
    key.layout_profile = PipelineLayoutProfile::Global_Set0_Only;
    key.render_state_id = 3;
    key.depth_format = static_cast<uint32_t>(vk::Format::eD32Sfloat);
    key.rt_format = static_cast<uint32_t>(context.swapchain->Format());
    key.viewport_width = context.swapchain->Extent().width;
    key.viewport_height = context.swapchain->Extent().height;

    vk::RenderPass const compatibility_render_pass = context.compatibility_load_render_pass != vk::RenderPass{}
        ? context.compatibility_load_render_pass
        : context.compatibility_render_pass;
    uint32_t const pipeline_id =
        context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, compatibility_render_pass);
    if (pipeline_id == 0) {
        LOGE("SkyboxPass pipeline create failed");
        EndSwapchainRendering(context);
        return;
    }
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        LOGE("SkyboxPass pipeline lookup failed");
        EndSwapchainRendering(context);
        return;
    }
    LOGI("SkyboxPass pipeline=%u", pipeline_id);

    context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

    glm::mat4 const world{1.0f};
    context.command_buffer.pushConstants(pipeline->Layout(),
                                         vk::ShaderStageFlagBits::eVertex,
                                         0,
                                         sizeof(glm::mat4),
                                         &world);

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

    LOGI("SkyboxPass drawing: env=%s depth=%s",
         g_shared_environment_maps.environment_cubemap.IsInitialized() ? "bound" : "missing",
         context.current_depth_texture != nullptr && context.current_depth_texture->IsInitialized() ? "present" : "missing");

    vk::DeviceSize offset = 0;
    context.command_buffer.bindVertexBuffers(0, mesh->vertex_buffer->Handle(), offset);
    if (mesh->index_buffer && mesh->index_buffer->IsInitialized() && mesh->index_count > 0) {
        context.command_buffer.bindIndexBuffer(mesh->index_buffer->Handle(), 0, vk::IndexType::eUint32);
        context.command_buffer.drawIndexed(mesh->index_count, 1, 0, 0, 0);
    } else {
        context.command_buffer.draw(mesh->vertex_count, 1, 0, 0);
    }
    LOGI("SkyboxPass draw submitted");

    EndSwapchainRendering(context);
    LOGI("SkyboxPass end");
}

} // namespace ave::render
