#include "ave/render/RenderPasses.h"

#include "ave/render/RenderPassCommon.h"

namespace ave::render {

PassDataFilter ComputePass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::Compute;
    return filter;
}

void ComputePass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    using namespace detail;

    (void)view;
    uint32_t const object_count = static_cast<uint32_t>(view.renderables.size());

    if (g_culling_visibility.size() != object_count) {
        g_culling_visibility.assign(object_count, 1u);
    }

    if (object_count == 0) {
        return;
    }

    bool const has_vk =
        context.vk != nullptr && context.swapchain != nullptr && context.command_buffer != vk::CommandBuffer{};

    uint64_t const frame_index = context.frame ? context.frame->frame_index : 0;
    uint32_t const buf_idx = static_cast<uint32_t>(frame_index % 2);

    if (has_vk) {
        if (frame_index >= 2 && visibility_buffers_[buf_idx].IsInitialized()) {
            uint32_t const* mapped_vis = static_cast<uint32_t const*>(visibility_buffers_[buf_idx].MappedData());
            if (mapped_vis != nullptr) {
                uint32_t const elements_to_copy = std::min(object_count, visibility_buffers_[buf_idx].Size() / static_cast<uint32_t>(sizeof(uint32_t)));
                for (uint32_t i = 0; i < elements_to_copy; ++i) {
                    g_culling_visibility[i] = mapped_vis[i];
                }
                for (uint32_t i = elements_to_copy; i < object_count; ++i) {
                    g_culling_visibility[i] = 1u;
                }
            }
        }
    }

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
                if (!r) {
                    continue;
                }
                glm::vec3 center = glm::vec3(r->world[3]);
                float radius = 1.5f;
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

    if (has_vk) {
        auto& shader_mgr = context.resources->GetShaderManager();
        if (g_culling_shader_id == 0) {
            g_culling_shader_id = shader_mgr.LoadComputeShader("compiled_shaders/culling.comp.spv");
        }

        struct InstanceData {
            glm::vec4 position_radius;
        };
        std::vector<InstanceData> cpu_instances(object_count);
        for (uint32_t i = 0; i < object_count; ++i) {
            auto const* r = view.renderables[i];
            cpu_instances[i].position_radius = glm::vec4(r ? glm::vec3(r->world[3]) : glm::vec3(0.0f), 1.5f);
        }

        uint32_t const inst_size = object_count * sizeof(InstanceData);
        uint32_t const vis_size = object_count * sizeof(uint32_t);

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

        instances_buffers_[buf_idx].UpdateData(*context.vk, cpu_instances.data(), inst_size);

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

        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 0, instances_buffers_[buf_idx].Handle(), 0, inst_size);
        desc_alloc.UpdateStorageBuffer(descriptor_set_ids_[buf_idx], 1, visibility_buffers_[buf_idx].Handle(), 0, vis_size);

        PipelineKey pipe_key;
        pipe_key.shader_id = g_culling_shader_id;
        pipe_key.layout_profile = PipelineLayoutProfile::ComputeCulling_Set0_Only;

        uint32_t const pipeline_id = context.pipelines->GetPipelineCache().GetOrCreatePipeline(pipe_key, context.compatibility_render_pass);
        auto const* pipeline = context.pipelines->GetPipelineCache().GetPipeline(pipeline_id);
        if (pipeline) {
            vk::CommandBuffer cmd = context.command_buffer;

            cmd.bindPipeline(pipeline->BindPoint(), pipeline->Handle());

            vk::DescriptorSet desc_set = desc_alloc.GetHandle(descriptor_set_ids_[buf_idx]);
            if (desc_set) {
                cmd.bindDescriptorSets(
                    pipeline->BindPoint(),
                    pipeline->Layout(),
                    0, 1, &desc_set, 0, nullptr
                );
            }

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

            uint32_t const group_count = (object_count + 15) / 16;
            cmd.dispatch(group_count, 1, 1);

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

    uint32_t visible_count = 0;
    for (uint32_t i = 0; i < object_count; ++i) {
        if (g_culling_visibility[i] != 0) {
            visible_count++;
        }
    }
    (void)visible_count;
}

} // namespace ave::render
