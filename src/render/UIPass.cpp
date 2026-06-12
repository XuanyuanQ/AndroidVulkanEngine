#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {


void UIPass::Reset(vkfw::VkContext* ctx)
{
    texture_descriptor_sets_.clear();
    ui_shader_id_ = 0;
    if (ctx != nullptr) {
        for (auto& buffer : ui_vertex_buffers_) {
            if (buffer.IsInitialized()) {
                buffer.Shutdown(*ctx);
            }
        }
        for (auto& buffer : ui_index_buffers_) {
            if (buffer.IsInitialized()) {
                buffer.Shutdown(*ctx);
            }
        }
        if (fallback_white_texture_.IsInitialized()) {
            fallback_white_texture_.Shutdown(*ctx);
        }
    }
    ui_vertex_buffers_.clear();
    ui_index_buffers_.clear();
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
    using namespace detail;

    if (view.ui_items.empty()) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr &&
        context.color_target.IsValid() &&
        context.command_buffer != vk::CommandBuffer{} &&
        context.pipelines != nullptr &&
        context.resources != nullptr;

    uint32_t const image_count = context.frame_resource_count != 0 ? context.frame_resource_count : 1u;
    uint32_t const buf_idx = context.frame_resource_index;
    if (image_count == 0 || buf_idx >= image_count) {
        return;
    }
    if (ui_vertex_buffers_.size() != image_count) {
        ui_vertex_buffers_.resize(image_count);
    }
    if (ui_index_buffers_.size() != image_count) {
        ui_index_buffers_.resize(image_count);
    }

    auto& texture_mgr = context.resources->GetTextureManager();
    auto& desc_cache = context.pipelines->GetDescriptorSetLayoutCache();
    auto& desc_alloc = context.pipelines->GetDescriptorAllocator();
    std::vector<ave::render::UiVertex> vertices;
    std::vector<uint32_t> indices;
    struct UiDrawRange {
        uint32_t first_index = 0;
        uint32_t index_count = 0;
        uint32_t texture_index = 0;
    };
    std::vector<UiDrawRange> draw_ranges;
    vertices.reserve(view.ui_items.size() * 4);
    indices.reserve(view.ui_items.size() * 6);
    draw_ranges.reserve(view.ui_items.size());

    float const width = has_vk ? static_cast<float>(context.color_target.extent.width) : 1080.0f;
    float const height = has_vk ? static_cast<float>(context.color_target.extent.height) : 1920.0f;
    float const aspect_ratio = (width > 0.0f) ? (height / width) : (9.0f / 16.0f);

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

    std::vector<uint32_t> texture_runtime_ids(16, 0);
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

    for (auto const* item : view.ui_items) {
        if (!item || !item->visible) {
            continue;
        }

        uint32_t texture_index = 0;
        if (!item->texture_id.empty()) {
            auto it = std::find(unique_texture_paths.begin(), unique_texture_paths.end(), item->texture_id);
            if (it != unique_texture_paths.end()) {
                texture_index = static_cast<uint32_t>(1 + std::distance(unique_texture_paths.begin(), it));
            }
        }

        uint32_t const first_index = static_cast<uint32_t>(indices.size());
        AppendUiQuad(vertices, indices, *item, texture_index, aspect_ratio);
        draw_ranges.push_back(UiDrawRange{
            .first_index = first_index,
            .index_count = static_cast<uint32_t>(indices.size()) - first_index,
            .texture_index = texture_index,
        });
    }

    if (!has_vk || vertices.empty() || indices.empty()) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (ui_shader_id_ == 0) {
        ui_shader_id_ = shader_mgr.LoadShader("compiled_shaders/ui_textured");
        if (ui_shader_id_ == 0) {
            LOGE("UIPass failed to load ui_textured shader");
            return;
        }
    }

    uint32_t const vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(ave::render::UiVertex));
    uint32_t const index_bytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

    if (!ui_vertex_buffers_[buf_idx].IsInitialized() || ui_vertex_buffers_[buf_idx].Size() < vertex_bytes) {
        if (ui_vertex_buffers_[buf_idx].IsInitialized()) {
            ui_vertex_buffers_[buf_idx].Shutdown(*context.vk);
        }
        if (!ui_vertex_buffers_[buf_idx].Init(*context.vk, vkfw::BufferInfo{
                                                     .size = vertex_bytes,
                                                     .usage = vkfw::BufferUsage::Vertex,
                                                     .mappable = true,
                                                 })) {
            LOGE("UIPass failed to create vertex buffer");
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
            LOGE("UIPass failed to create index buffer");
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
    if (!BeginRenderTargetRendering(context, clear, false)) {
        LOGE("UIPass failed to begin rendering");
        return;
    }

    ave::resource::MeshRuntime ui_mesh{};
    ui_mesh.vertex_stride = sizeof(ave::render::UiVertex);

    PipelineKey key = MakePipelineKey(ui_shader_id_, ui_mesh);
    key.vertex_layout_id = 2;
    key.layout_profile = PipelineLayoutProfile::Texture_Set0_Only;
    key.render_state_id = 2;
    key.rt_format = static_cast<uint32_t>(context.color_target.format);
    key.viewport_width = context.color_target.extent.width;
    key.viewport_height = context.color_target.extent.height;

    vk::RenderPass const ui_compatibility_render_pass =
        context.color_target.compatibility_load_render_pass != vk::RenderPass{}
            ? context.color_target.compatibility_load_render_pass
            : context.color_target.compatibility_render_pass;
    uint32_t const pipeline_id =
        context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, ui_compatibility_render_pass);
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        EndSwapchainRendering(context);
        LOGE("UIPass failed to create pipeline");
        return;
    }

    context.command_buffer.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

    vk::DeviceSize offset = 0;
    context.command_buffer.bindVertexBuffers(0, ui_vertex_buffers_[buf_idx].Handle(), offset);
    context.command_buffer.bindIndexBuffer(ui_index_buffers_[buf_idx].Handle(), 0, vk::IndexType::eUint32);

    uint32_t const texture_layout_id = desc_cache.GetOrCreateLayout(MakeTextureSetLayoutKey());
    vk::Sampler const sampler = GetUiSampler(*context.vk);
    EnsureFallbackWhiteTexture(*context.vk, fallback_white_texture_);

    std::array<uint32_t, 16> descriptor_set_ids{};
    for (uint32_t slot = 0; slot < descriptor_set_ids.size(); ++slot) {
        uint32_t const runtime_id = texture_runtime_ids[slot];
        uint32_t const descriptor_key = (buf_idx << 8u) | slot;
        uint32_t descriptor_set_id = texture_descriptor_sets_[descriptor_key];
        if (descriptor_set_id == 0) {
            descriptor_set_id = desc_alloc.AllocateDescriptorSet(texture_layout_id);
            texture_descriptor_sets_[descriptor_key] = descriptor_set_id;
        }
        descriptor_set_ids[slot] = descriptor_set_id;

        vkfw::VkTexture const* texture = ResolveTextureOrFallback(*context.vk, texture_mgr, runtime_id, fallback_white_texture_);
        if (descriptor_set_id != 0 && texture != nullptr) {
            desc_alloc.UpdateImageSampler(descriptor_set_id,
                                          0,
                                          sampler,
                                          texture->View(),
                                          vk::ImageLayout::eShaderReadOnlyOptimal);
        }
    }

    for (UiDrawRange const& range : draw_ranges) {
        if (range.index_count == 0) {
            continue;
        }
        uint32_t const descriptor_set_id =
            range.texture_index < descriptor_set_ids.size() ? descriptor_set_ids[range.texture_index] : descriptor_set_ids[0];
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
        context.command_buffer.drawIndexed(range.index_count, 1, range.first_index, 0, 0);
    }

    EndSwapchainRendering(context);
}

} // namespace ave::render
