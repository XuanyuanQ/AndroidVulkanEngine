#include "ave/render/RenderPasses.h"

namespace ave::render {

PassDataFilter ToneMappingPass::GetDataFilter() const
{
    PassDataFilter filter{};
    filter.pass_bit = core::RenderPassBit::ToneMapping;
    return filter;
}

void ToneMappingPass::Execute(RenderPassContext const& context, PassExecutionView const& view)
{
    (void)context;
    (void)view;
}

} // namespace ave::render
