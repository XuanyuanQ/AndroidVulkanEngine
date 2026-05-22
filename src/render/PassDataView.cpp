#include "ave/render/RenderPass.h"

namespace ave::render {

namespace {

bool PassesMaterialFilter(core::FrameRenderableData const& renderable, PassDataFilter const& filter)
{
    if (!filter.material_id.has_value()) {
        return true;
    }
    return renderable.material_id == *filter.material_id;
}

bool PassesOpaqueTransparent(core::FrameRenderableData const& renderable, PassDataFilter const& filter)
{
    if (filter.opaque_only && filter.transparent_only) {
        return false;
    }
    if (filter.opaque_only && renderable.render_queue >= core::kQueueTransparent) {
        return false;
    }
    if (filter.transparent_only && renderable.render_queue < core::kQueueTransparent) {
        return false;
    }
    return true;
}

bool PassesRenderableFilter(core::FrameRenderableData const& renderable, PassDataFilter const& filter)
{
    if (!renderable.visible) {
        return false;
    }
    if ((renderable.layer_mask & filter.layer_mask) == 0u) {
        return false;
    }
    core::RenderPassBit check_bit = filter.pass_bit;
    if (check_bit == core::RenderPassBit::Compute) {
        check_bit = core::RenderPassBit::ForwardOpaque;
    }
    if (!core::HasPassBit(renderable.pass_mask, check_bit)) {
        return false;
    }
    if (filter.shadow_casters_only && !renderable.casts_shadow) {
        return false;
    }
    if (!PassesMaterialFilter(renderable, filter)) {
        return false;
    }
    return PassesOpaqueTransparent(renderable, filter);
}

bool PassesLightFilter(core::FrameLightData const& light, PassDataFilter const& filter)
{
    if (filter.pass_bit != core::RenderPassBit::Shadow) {
        return true;
    }
    if (!light.cast_shadows) {
        return false;
    }
    if (filter.light_group != 0u) {
        // Reserved: light groups not yet authored in FrameData.
        return true;
    }
    return true;
}

} // namespace

PassExecutionView BuildPassView(core::FrameData const& frame, PassDataFilter const& filter)
{
    PassExecutionView view;

    if (filter.pass_bit == core::RenderPassBit::UI) {
        view.ui_items.reserve(frame.ui_items.size());
        for (auto const& item : frame.ui_items) {
            if (!item.visible) {
                continue;
            }
            if ((item.layer_mask & filter.layer_mask) == 0u) {
                continue;
            }
            if (!core::HasPassBit(item.pass_mask, filter.pass_bit)) {
                continue;
            }
            view.ui_items.push_back(&item);
        }
        return view;
    }

    view.renderables.reserve(frame.renderables.size());
    for (auto const& renderable : frame.renderables) {
        if (!PassesRenderableFilter(renderable, filter)) {
            continue;
        }
        view.renderables.push_back(&renderable);
    }

    if (filter.pass_bit == core::RenderPassBit::Shadow ||
        filter.pass_bit == core::RenderPassBit::ForwardOpaque ||
        filter.pass_bit == core::RenderPassBit::ForwardTransparent) {
        view.lights.reserve(frame.lights.size());
        for (auto const& light : frame.lights) {
            if (!PassesLightFilter(light, filter)) {
                continue;
            }
            view.lights.push_back(&light);
        }
    }

    return view;
}

} // namespace ave::render
