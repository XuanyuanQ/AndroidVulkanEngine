#pragma once

#include "ave/render/RenderPass.h"
#include "ave/render/RenderWorld.h"
#include "ave/render/MaterialSystem.h"

#include <memory>

namespace ave::render {

// Depth Prepass
class DepthPrepass : public RenderPass {
public:
    DepthPrepass();
    ~DepthPrepass() override = default;

    std::string Name() const override { return "DepthPrepass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        filter.opaque_only = true; // Only opaque objects
        return filter;
    }
};

// Shadow Pass
class ShadowPass : public RenderPass {
public:
    ShadowPass();
    ~ShadowPass() override = default;

    std::string Name() const override { return "ShadowPass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        filter.opaque_only = true;
        filter.light_group = 1; // Only shadow-casting lights
        return filter;
    }
};

// PBR Pass
class PBRPass : public RenderPass {
public:
    PBRPass();
    ~PBRPass() override = default;

    std::string Name() const override { return "PBRPass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        filter.layer_mask = 0xFFFFFFFF;
        return filter;
    }
};

// Compute Pass (for particle systems or bloom)
class ComputePass : public RenderPass {
public:
    ComputePass();
    ~ComputePass() override = default;

    std::string Name() const override { return "ComputePass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        // Compute pass doesn't need renderable data
        return filter;
    }
};

// UI Pass
class UIPass : public RenderPass {
public:
    UIPass();
    ~UIPass() override = default;

    std::string Name() const override { return "UIPass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        filter.layer_mask = 0x00000001; // UI layer only
        return filter;
    }
};

// Tone Mapping Pass
class ToneMappingPass : public RenderPass {
public:
    ToneMappingPass();
    ~ToneMappingPass() override = default;

    std::string Name() const override { return "ToneMappingPass"; }
    void Execute(RenderPassContext const& context) override;
    
    PassDataFilter GetDataFilter() const override {
        PassDataFilter filter;
        // Tone mapping only needs the rendered texture, no scene data
        return filter;
    }
};

} // namespace ave::render
