#pragma once

#include "ave/core/FrameData.h"

#include <string>

namespace ave::render {

struct RenderPassContext {
    core::FrameData const* frame = nullptr;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual std::string Name() const = 0;
    virtual void Execute(RenderPassContext const& context) = 0;
};

} // namespace ave::render
