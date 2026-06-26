#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {
namespace {

struct UiDrawRange {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t texture_index = 0;
};

glm::vec3 ExtractCameraRight(glm::mat4 const& camera_world)
{
    return glm::normalize(glm::vec3{camera_world[0]});
}

glm::vec3 ExtractCameraUp(glm::mat4 const& camera_world)
{
    return glm::normalize(glm::vec3{camera_world[1]});
}

glm::vec3 ExtractCameraForward(glm::mat4 const& camera_world)
{
    return glm::normalize(-glm::vec3{camera_world[2]});
}

glm::vec3 ComputeStereoCenter(core::FrameData const& frame, core::FrameViewData const& fallback_view)
{
    if (frame.views.empty()) {
        return fallback_view.world_position;
    }

    glm::vec3 center{0.0f};
    for (auto const& view : frame.views) {
        center += view.world_position;
    }
    return center / static_cast<float>(frame.views.size());
}

core::FrameViewData const& StereoBasisView(core::FrameData const& frame, core::FrameViewData const& fallback_view)
{
    return frame.views.empty() ? fallback_view : frame.views.front();
}

bool AppendXRWorldUiQuad(std::vector<UiVertex>& vertices,
                         std::vector<uint32_t>& indices,
                         core::FrameUiData const& item,
                         uint32_t texture_index,
                         float ui_aspect_ratio,
                         core::FrameViewData const& frame_view,
                         core::FrameViewData const& basis_view,
                         glm::vec3 const& stereo_center)
{
    std::vector<UiVertex> screen_vertices;
    std::vector<uint32_t> screen_indices;
    detail::AppendUiQuad(screen_vertices, screen_indices, item, texture_index, ui_aspect_ratio);
    if (screen_vertices.size() != 4 || screen_indices.size() != 6) {
        return false;
    }

    glm::mat4 const camera_world = glm::inverse(basis_view.view);
    glm::vec3 const right = ExtractCameraRight(camera_world);
    glm::vec3 const up = ExtractCameraUp(camera_world);
    glm::vec3 const forward = ExtractCameraForward(camera_world);

    float constexpr panel_distance = 2.0f;
    float constexpr panel_half_height = 0.55f;
    float const panel_half_width = panel_half_height / std::max(ui_aspect_ratio, 0.01f);
    glm::vec3 const panel_center = stereo_center + forward * panel_distance - up * 0.05f;

    uint32_t const base_vertex = static_cast<uint32_t>(vertices.size());
    for (UiVertex vertex : screen_vertices) {
        glm::vec2 const panel_position{vertex.position.x, -vertex.position.y};
        glm::vec3 const world_position =
            panel_center +
            right * (panel_position.x * panel_half_width) +
            up * (panel_position.y * panel_half_height);

        glm::vec4 const clip = frame_view.view_projection * glm::vec4{world_position, 1.0f};
        if (clip.w <= 0.0001f) {
            return false;
        }
        vertex.position = glm::vec2{clip.x, clip.y} / clip.w;
        vertices.push_back(vertex);
    }

    for (uint32_t index : screen_indices) {
        indices.push_back(base_vertex + index);
    }
    return true;
}

} // namespace

void XRWorldUIPass::Reset(vkfw::VkContext* ctx)
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

PassDataFilter XRWorldUIPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::UI;
    filter.layer_mask = core::ToMask(core::RenderLayer::UI);
    return filter;
}

void XRWorldUIPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    if (context.view_count <= 1 || view.ui_items.empty()) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr &&
        context.color_target.IsValid() &&
        context.command_buffer != vk::CommandBuffer{} &&
        context.pipelines != nullptr &&
        context.resources != nullptr &&
        context.frame != nullptr;
    if (!has_vk) {
        return;
    }

    core::FrameViewData const* frame_view = CurrentFrameView(context);
    if (frame_view == nullptr) {
        return;
    }

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

    std::vector<std::string> unique_texture_paths;
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

    float const width = static_cast<float>(context.color_target.extent.width);
    float const height = static_cast<float>(context.color_target.extent.height);
    float const ui_aspect_ratio = (width > 0.0f) ? (height / width) : 1.0f;
    glm::vec3 const stereo_center = ComputeStereoCenter(*context.frame, *frame_view);
    core::FrameViewData const& basis_view = StereoBasisView(*context.frame, *frame_view);

    std::vector<UiVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UiDrawRange> draw_ranges;
    vertices.reserve(view.ui_items.size() * 4);
    indices.reserve(view.ui_items.size() * 6);
    draw_ranges.reserve(view.ui_items.size());

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
        if (!AppendXRWorldUiQuad(vertices,
                                 indices,
                                 *item,
                                 texture_index,
                                 ui_aspect_ratio,
                                 *frame_view,
                                 basis_view,
                                 stereo_center)) {
            continue;
        }
        draw_ranges.push_back(UiDrawRange{
            .first_index = first_index,
            .index_count = static_cast<uint32_t>(indices.size()) - first_index,
            .texture_index = texture_index,
        });
    }

    if (vertices.empty() || indices.empty()) {
        return;
    }

    auto& shader_mgr = context.resources->GetShaderManager();
    if (ui_shader_id_ == 0) {
        ui_shader_id_ = shader_mgr.LoadShader("compiled_shaders/ui_textured");
        if (ui_shader_id_ == 0) {
            LOGE("XRWorldUIPass failed to load ui_textured shader");
            return;
        }
    }

    uint32_t const vertex_bytes = static_cast<uint32_t>(vertices.size() * sizeof(UiVertex));
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
            LOGE("XRWorldUIPass failed to create vertex buffer");
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
            LOGE("XRWorldUIPass failed to create index buffer");
            return;
        }
    }

    ui_vertex_buffers_[buf_idx].UpdateData(*context.vk, vertices.data(), vertex_bytes);
    ui_index_buffers_[buf_idx].UpdateData(*context.vk, indices.data(), index_bytes);

    vk::ClearValue clear{};
    clear.color.float32[3] = 0.0f;
    if (!BeginRenderTargetRendering(context, clear, false)) {
        LOGE("XRWorldUIPass failed to begin rendering");
        return;
    }

    ave::resource::MeshRuntime ui_mesh{};
    ui_mesh.vertex_stride = sizeof(UiVertex);

    PipelineKey key = MakePipelineKey(ui_shader_id_, ui_mesh);
    key.vertex_layout_id = 2;
    key.layout_profile = PipelineLayoutProfile::Texture_Set0_Only;
    key.render_state_id = 2;
    key.rt_format = static_cast<uint32_t>(context.color_target.format);
    key.viewport_width = context.color_target.extent.width;
    key.viewport_height = context.color_target.extent.height;

    vk::RenderPass const compatibility_render_pass =
        context.color_target.compatibility_load_render_pass != vk::RenderPass{}
            ? context.color_target.compatibility_load_render_pass
            : context.color_target.compatibility_render_pass;
    uint32_t const pipeline_id =
        context.pipelines->GetPipelineCache().GetOrCreatePipeline(key, compatibility_render_pass);
    auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
    if (!pipeline) {
        EndSwapchainRendering(context);
        LOGE("XRWorldUIPass failed to create pipeline");
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
