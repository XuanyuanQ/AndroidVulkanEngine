#include "ave/scene/SceneWorld.h"

namespace ave::scene {

uint32_t SceneWorld::AddRenderable(std::string object_id, std::string debug_name, std::string mesh_id, std::string material_id)
{
    core::FrameRenderableData renderable{};
    renderable.object_id = std::move(object_id);
    renderable.debug_name = std::move(debug_name);
    renderable.mesh_id = std::move(mesh_id);
    renderable.material_id = std::move(material_id);
    renderables_.push_back(std::move(renderable));
    return static_cast<uint32_t>(renderables_.size() - 1);
}

uint32_t SceneWorld::AddPointLight(float x, float y, float z, float intensity)
{
    core::FrameLightData light{};
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    light.intensity = intensity;
    lights_.push_back(light);
    return static_cast<uint32_t>(lights_.size() - 1);
}

void SceneWorld::BuildFrameData(uint64_t frame_index, core::FrameData& out_frame) const
{
    out_frame.frame_index = frame_index;
    out_frame.renderables = renderables_;
    out_frame.lights = lights_;
    out_frame.ui_items.clear();
    out_frame.resources.meshes.clear();
    out_frame.resources.materials.clear();
    out_frame.resources.textures.clear();

    out_frame.resources.meshes.reserve(out_frame.renderables.size());
    out_frame.resources.materials.reserve(out_frame.renderables.size());
    for (auto const& renderable : out_frame.renderables) {
        if (!renderable.mesh_id.empty()) {
            out_frame.resources.meshes.push_back(renderable.mesh_id);
        }
        if (!renderable.material_id.empty()) {
            out_frame.resources.materials.push_back(renderable.material_id);
        }
    }
}

} // namespace ave::scene
