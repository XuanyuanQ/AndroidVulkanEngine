#include "ave/scene/SceneWorld.h"

namespace ave::scene {

uint32_t SceneWorld::AddRenderable(std::string debug_name, uint32_t mesh_id, uint32_t material_id)
{
    core::RenderableData renderable{};
    renderable.debug_name = std::move(debug_name);
    renderable.mesh_id = mesh_id;
    renderable.material_id = material_id;
    renderables_.push_back(std::move(renderable));
    return static_cast<uint32_t>(renderables_.size() - 1);
}

uint32_t SceneWorld::AddPointLight(float x, float y, float z, float intensity)
{
    core::LightData light{};
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
}

} // namespace ave::scene
