#include "ave/render/RenderPassCommon.h"

#include <cmath>
#include <numbers>

namespace ave::render::detail {

std::vector<uint32_t> g_culling_visibility;
uint32_t g_culling_shader_id = 0;
std::unique_ptr<vk::raii::RenderPass> g_compatibility_shadow_render_pass;
std::unique_ptr<vk::raii::Framebuffer> g_compatibility_shadow_framebuffer;
vk::ImageView g_last_shadow_image_view = {};
SharedEnvironmentMaps g_shared_environment_maps;
CpuCubemapSource g_maskonaive_source;

namespace {

std::unique_ptr<vk::raii::Sampler>& CommonSamplerObject()
{
    static std::unique_ptr<vk::raii::Sampler> sampler;
    return sampler;
}

vk::Sampler& CommonSamplerHandle()
{
    static vk::Sampler handle{};
    return handle;
}

vk::Sampler& CommonSamplerStorage(vkfw::VkContext& ctx)
{
    auto& sampler = CommonSamplerObject();
    if (!sampler) {
        vk::SamplerCreateInfo create_info{};
        create_info.magFilter = vk::Filter::eLinear;
        create_info.minFilter = vk::Filter::eLinear;
        create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        create_info.addressModeW = vk::SamplerAddressMode::eRepeat;
        create_info.maxLod = VK_LOD_CLAMP_NONE;
        sampler = std::make_unique<vk::raii::Sampler>(ctx.Device(), create_info);
    }
    auto& handle = CommonSamplerHandle();
    handle = **sampler;
    return handle;
}

void ResetCommonSamplerStorage()
{
    auto& sampler = CommonSamplerObject();
    sampler.reset();
    CommonSamplerHandle() = nullptr;
}

std::unique_ptr<vk::raii::Sampler>& ShadowSamplerObject()
{
    static std::unique_ptr<vk::raii::Sampler> sampler;
    return sampler;
}

vk::Sampler& ShadowSamplerHandle()
{
    static vk::Sampler handle{};
    return handle;
}

vk::Sampler& ShadowSamplerStorage(vkfw::VkContext& ctx)
{
    auto& sampler = ShadowSamplerObject();
    if (!sampler) {
        vk::SamplerCreateInfo create_info{};
        create_info.magFilter = vk::Filter::eLinear;
        create_info.minFilter = vk::Filter::eLinear;
        create_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        create_info.addressModeU = vk::SamplerAddressMode::eClampToBorder;
        create_info.addressModeV = vk::SamplerAddressMode::eClampToBorder;
        create_info.addressModeW = vk::SamplerAddressMode::eClampToBorder;
        create_info.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        create_info.maxLod = VK_LOD_CLAMP_NONE;
        create_info.compareEnable = VK_FALSE;
        create_info.compareOp = vk::CompareOp::eLessOrEqual;
        sampler = std::make_unique<vk::raii::Sampler>(ctx.Device(), create_info);
    }
    auto& handle = ShadowSamplerHandle();
    handle = **sampler;
    return handle;
}

void ResetShadowSamplerStorage()
{
    auto& sampler = ShadowSamplerObject();
    sampler.reset();
    ShadowSamplerHandle() = nullptr;
}

glm::vec3 NormalizeSafe(glm::vec3 value, glm::vec3 fallback = glm::vec3{0.0f, 1.0f, 0.0f})
{
    float const len_sq = glm::dot(value, value);
    if (len_sq <= 0.000001f) {
        return fallback;
    }
    return value / std::sqrt(len_sq);
}

glm::vec3 FaceDirection(uint32_t face, float u, float v)
{
    switch (face) {
        case 0: return NormalizeSafe({1.0f, -v, -u});
        case 1: return NormalizeSafe({-1.0f, -v, u});
        case 2: return NormalizeSafe({u, 1.0f, v});
        case 3: return NormalizeSafe({u, -1.0f, -v});
        case 4: return NormalizeSafe({u, -v, 1.0f});
        case 5: return NormalizeSafe({-u, -v, -1.0f});
        default: return {0.0f, 1.0f, 0.0f};
    }
}

bool DirectionToFaceUV(glm::vec3 direction, uint32_t& out_face, float& out_u, float& out_v)
{
    direction = NormalizeSafe(direction);
    glm::vec3 const abs_dir = glm::abs(direction);

    if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) {
        if (direction.x >= 0.0f) {
            out_face = 0;
            out_u = -direction.z / abs_dir.x;
            out_v = -direction.y / abs_dir.x;
        } else {
            out_face = 1;
            out_u = direction.z / abs_dir.x;
            out_v = -direction.y / abs_dir.x;
        }
        return true;
    }

    if (abs_dir.y >= abs_dir.x && abs_dir.y >= abs_dir.z) {
        if (direction.y >= 0.0f) {
            out_face = 2;
            out_u = direction.x / abs_dir.y;
            out_v = direction.z / abs_dir.y;
        } else {
            out_face = 3;
            out_u = direction.x / abs_dir.y;
            out_v = -direction.z / abs_dir.y;
        }
        return true;
    }

    if (direction.z >= 0.0f) {
        out_face = 4;
        out_u = direction.x / abs_dir.z;
        out_v = -direction.y / abs_dir.z;
    } else {
        out_face = 5;
        out_u = -direction.x / abs_dir.z;
        out_v = -direction.y / abs_dir.z;
    }
    return true;
}

glm::vec4 SampleCubemapFace(CpuCubemapFace const& face, float u, float v)
{
    if (face.width == 0 || face.height == 0 || face.pixels.empty()) {
        return glm::vec4{0.0f};
    }

    float const fx = std::clamp((u + 1.0f) * 0.5f * static_cast<float>(face.width - 1), 0.0f, static_cast<float>(face.width - 1));
    float const fy = std::clamp((v + 1.0f) * 0.5f * static_cast<float>(face.height - 1), 0.0f, static_cast<float>(face.height - 1));
    uint32_t const x0 = static_cast<uint32_t>(std::floor(fx));
    uint32_t const y0 = static_cast<uint32_t>(std::floor(fy));
    uint32_t const x1 = std::min(x0 + 1u, face.width - 1u);
    uint32_t const y1 = std::min(y0 + 1u, face.height - 1u);
    float const tx = fx - static_cast<float>(x0);
    float const ty = fy - static_cast<float>(y0);

    auto const at = [&](uint32_t x, uint32_t y) -> glm::vec4 const& {
        return face.pixels[static_cast<size_t>(y) * face.width + x];
    };

    glm::vec4 const c00 = at(x0, y0);
    glm::vec4 const c10 = at(x1, y0);
    glm::vec4 const c01 = at(x0, y1);
    glm::vec4 const c11 = at(x1, y1);
    glm::vec4 const cx0 = glm::mix(c00, c10, tx);
    glm::vec4 const cx1 = glm::mix(c01, c11, tx);
    return glm::mix(cx0, cx1, ty);
}

glm::vec3 SampleCubemapSource(CpuCubemapSource const& source, glm::vec3 direction)
{
    uint32_t face = 0;
    float u = 0.0f;
    float v = 0.0f;
    if (!DirectionToFaceUV(direction, face, u, v)) {
        return glm::vec3{0.0f};
    }
    if (face >= source.faces.size()) {
        return glm::vec3{0.0f};
    }
    glm::vec4 const color = SampleCubemapFace(source.faces[face], u, v);
    return glm::vec3{color.r, color.g, color.b};
}

void BuildTangentBasis(glm::vec3 const& normal, glm::vec3& tangent, glm::vec3& bitangent)
{
    glm::vec3 const up = std::abs(normal.z) < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{1.0f, 0.0f, 0.0f};
    tangent = NormalizeSafe(glm::cross(up, normal), glm::vec3{1.0f, 0.0f, 0.0f});
    bitangent = NormalizeSafe(glm::cross(normal, tangent), glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::vec2 Hammersley(uint32_t i, uint32_t n)
{
    auto radical_inverse = [](uint32_t bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(bits) * 2.3283064365386963e-10f;
    };
    return {static_cast<float>(i) / static_cast<float>(n), radical_inverse(i)};
}

glm::vec3 SampleHemisphereCosine(glm::vec2 xi, glm::vec3 const& normal)
{
    float const r = std::sqrt(xi.x);
    float const phi = 2.0f * std::numbers::pi_v<float> * xi.y;
    glm::vec3 const local{
        r * std::cos(phi),
        r * std::sin(phi),
        std::sqrt(std::max(0.0f, 1.0f - xi.x)),
    };

    glm::vec3 tangent;
    glm::vec3 bitangent;
    BuildTangentBasis(normal, tangent, bitangent);
    return NormalizeSafe(tangent * local.x + bitangent * local.y + normal * local.z, normal);
}

glm::vec3 ImportanceSampleGGX(glm::vec2 xi, glm::vec3 const& normal, float roughness)
{
    float const a = roughness * roughness;
    float const phi = 2.0f * std::numbers::pi_v<float> * xi.x;
    float const cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float const sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    glm::vec3 const h{
        std::cos(phi) * sinTheta,
        std::sin(phi) * sinTheta,
        cosTheta,
    };

    glm::vec3 tangent;
    glm::vec3 bitangent;
    BuildTangentBasis(normal, tangent, bitangent);
    return NormalizeSafe(tangent * h.x + bitangent * h.y + normal * h.z, normal);
}

glm::vec3 SampleProceduralEnvironment(glm::vec3 direction, glm::vec3 clear_color, glm::vec3 ambient_color)
{
    auto const smoothstepf = [](float edge0, float edge1, float x) {
        float const t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    direction = NormalizeSafe(direction);
    float const hemi = std::clamp(direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 const sky = glm::mix(clear_color, ambient_color, 0.15f) * glm::vec3{0.95f, 1.05f, 1.25f};
    glm::vec3 const horizon = glm::mix(clear_color, ambient_color, 0.35f) * glm::vec3{0.88f, 0.96f, 1.05f};
    glm::vec3 const ground = glm::mix(clear_color, ambient_color, 0.80f) * glm::vec3{0.20f, 0.24f, 0.28f};
    glm::vec3 const sky_band = glm::mix(horizon, sky, smoothstepf(0.10f, 0.95f, hemi));
    glm::vec3 const ground_band = glm::mix(ground, horizon, smoothstepf(0.0f, 0.5f, hemi));
    glm::vec3 color = glm::mix(ground_band, sky_band, hemi);

    glm::vec3 const sun_dir = NormalizeSafe(glm::vec3{0.35f, 0.85f, 0.25f});
    float const sun = std::pow(std::max(glm::dot(direction, sun_dir), 0.0f), 256.0f);
    color += sun * glm::vec3{2.0f, 2.0f, 2.1f};
    return color;
}

bool LoadMaskonaiveCubemapSource(ave::resource::TextureManager const& texture_mgr, CpuCubemapSource& out_source)
{
    std::array<std::string, 6> const face_paths{
        "textures/Maskonaive2/posx.jpg",
        "textures/Maskonaive2/negx.jpg",
        "textures/Maskonaive2/posy.jpg",
        "textures/Maskonaive2/negy.jpg",
        "textures/Maskonaive2/posz.jpg",
        "textures/Maskonaive2/negz.jpg",
    };

    out_source = {};
    uint32_t reference_width = 0;
    uint32_t reference_height = 0;

    for (size_t face_index = 0; face_index < face_paths.size(); ++face_index) {
        std::vector<std::uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        if (!texture_mgr.LoadImagePixels(face_paths[face_index], pixels, width, height)) {
            LOGW("Failed to load cubemap face: %s", face_paths[face_index].c_str());
            return false;
        }

        if (reference_width == 0) {
            reference_width = width;
            reference_height = height;
        } else if (reference_width != width || reference_height != height) {
            LOGW("Cubemap face size mismatch: %s", face_paths[face_index].c_str());
            return false;
        }

        auto& face = out_source.faces[face_index];
        face.width = width;
        face.height = height;
        face.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < face.pixels.size(); ++i) {
            size_t const base = i * 4u;
            face.pixels[i] = glm::vec4{
                static_cast<float>(pixels[base + 0]) / 255.0f,
                static_cast<float>(pixels[base + 1]) / 255.0f,
                static_cast<float>(pixels[base + 2]) / 255.0f,
                static_cast<float>(pixels[base + 3]) / 255.0f,
            };
        }
    }

    out_source.ready = true;
    return true;
}

void GenerateProceduralCubemapFace(std::vector<glm::vec4>& out,
                                   uint32_t size,
                                   uint32_t face,
                                   glm::vec3 clear_color,
                                   glm::vec3 ambient_color)
{
    out.resize(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float const u = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size)) - 1.0f;
            float const v = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size)) - 1.0f;
            glm::vec3 const dir = FaceDirection(face, u, v);
            glm::vec3 const color = SampleProceduralEnvironment(dir, clear_color, ambient_color);
            out[static_cast<size_t>(y) * size + x] = glm::vec4{color, 1.0f};
        }
    }
}

glm::vec3 IntegrateDiffuseIrradiance(glm::vec3 const& normal,
                                     CpuCubemapSource const* source,
                                     glm::vec3 clear_color,
                                     glm::vec3 ambient_color)
{
    constexpr uint32_t kSampleCount = 32;
    glm::vec3 result{0.0f};
    for (uint32_t i = 0; i < kSampleCount; ++i) {
        glm::vec2 const xi = Hammersley(i, kSampleCount);
        glm::vec3 const sample_dir = SampleHemisphereCosine(xi, normal);
        result += source && source->ready
            ? SampleCubemapSource(*source, sample_dir)
            : SampleProceduralEnvironment(sample_dir, clear_color, ambient_color);
    }
    return result / static_cast<float>(kSampleCount);
}

glm::vec3 IntegratePrefilteredEnvironment(glm::vec3 const& normal,
                                          float roughness,
                                          CpuCubemapSource const* source,
                                          glm::vec3 clear_color,
                                          glm::vec3 ambient_color)
{
    constexpr uint32_t kSampleCount = 64;
    glm::vec3 result{0.0f};
    float total_weight = 0.0f;

    for (uint32_t i = 0; i < kSampleCount; ++i) {
        glm::vec2 const xi = Hammersley(i, kSampleCount);
        glm::vec3 const h = ImportanceSampleGGX(xi, normal, roughness);
        glm::vec3 const l = NormalizeSafe(2.0f * glm::dot(normal, h) * h - normal, normal);
        float const n_dot_l = std::max(glm::dot(normal, l), 0.0f);
        if (n_dot_l > 0.0f) {
            result += (source && source->ready
                ? SampleCubemapSource(*source, l)
                : SampleProceduralEnvironment(l, clear_color, ambient_color)) * n_dot_l;
            total_weight += n_dot_l;
        }
    }

    if (total_weight <= 0.00001f) {
        return source && source->ready
            ? SampleCubemapSource(*source, normal)
            : SampleProceduralEnvironment(normal, clear_color, ambient_color);
    }
    return result / total_weight;
}

glm::vec2 IntegrateBrdf(float n_dot_v, float roughness)
{
    constexpr uint32_t kSampleCount = 64;
    glm::vec3 const v{std::sqrt(std::max(0.0f, 1.0f - n_dot_v * n_dot_v)), 0.0f, n_dot_v};
    float a = 0.0f;
    float b = 0.0f;

    for (uint32_t i = 0; i < kSampleCount; ++i) {
        glm::vec2 const xi = Hammersley(i, kSampleCount);
        glm::vec3 const h = ImportanceSampleGGX(xi, glm::vec3{0.0f, 0.0f, 1.0f}, roughness);
        glm::vec3 const l = NormalizeSafe(2.0f * glm::dot(v, h) * h - v, glm::vec3{0.0f, 0.0f, 1.0f});

        float const n_dot_l = std::max(l.z, 0.0f);
        float const n_dot_h = std::max(h.z, 0.0f);
        float const v_dot_h = std::max(glm::dot(v, h), 0.0f);

        if (n_dot_l > 0.0f) {
            float const g = (2.0f * n_dot_h * n_dot_v / std::max(v_dot_h, 0.0001f));
            float const g_vis = std::min(1.0f, std::min(g, 2.0f * n_dot_h * n_dot_v / std::max(v_dot_h, 0.0001f)));
            float const fc = std::pow(1.0f - v_dot_h, 5.0f);
            a += (1.0f - fc) * g_vis;
            b += fc * g_vis;
        }
    }

    return {a / static_cast<float>(kSampleCount), b / static_cast<float>(kSampleCount)};
}

void UploadCubemapFace(vkfw::VkContext& ctx,
                       vkfw::VkTexture& texture,
                       std::vector<glm::vec4> const& face_pixels,
                       uint32_t mip_level,
                       uint32_t face_index)
{
    texture.UpdateCubeFaceData(ctx,
                               face_pixels.data(),
                               static_cast<uint32_t>(face_pixels.size() * sizeof(glm::vec4)),
                               face_index,
                               mip_level);
}

glm::vec3 ClampUiPosition(glm::vec2 const& position, float depth)
{
    return glm::vec3{
        std::clamp(position.x, -1.0f, 1.0f),
        std::clamp(position.y, -1.0f, 1.0f),
        std::clamp(depth, -1.0f, 1.0f),
    };
}

} // namespace

vk::Sampler GetCommonSampler(vkfw::VkContext& ctx)
{
    return CommonSamplerStorage(ctx);
}

vk::Sampler GetShadowSampler(vkfw::VkContext& ctx)
{
    return ShadowSamplerStorage(ctx);
}

void ResetCommonSampler()
{
    ResetCommonSamplerStorage();
}

void ResetShadowSampler()
{
    ResetShadowSamplerStorage();
}

void EnsureFallbackWhiteTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture)
{
    if (texture.IsInitialized()) {
        return;
    }

    uint32_t const white_pixel = 0xFFFFFFFFu;
    texture.Init(ctx, vkfw::TextureInfo{
                          .width = 1,
                          .height = 1,
                          .mip_levels = 1,
                          .format = vkfw::TextureFormat::R8G8B8A8_UNORM,
                          .usage = vkfw::TextureUsage::Sampled,
                          .mipmap = false,
                      });
    texture.UpdateData(ctx, &white_pixel, sizeof(white_pixel));
}

void EnsureFallbackNormalTexture(vkfw::VkContext& ctx, vkfw::VkTexture& texture)
{
    if (texture.IsInitialized()) {
        return;
    }

    std::array<std::uint8_t, 4> const normal_pixel{128u, 128u, 255u, 255u};
    texture.Init(ctx, vkfw::TextureInfo{
                          .width = 1,
                          .height = 1,
                          .mip_levels = 1,
                          .format = vkfw::TextureFormat::R8G8B8A8_UNORM,
                          .usage = vkfw::TextureUsage::Sampled,
                          .mipmap = false,
                      });
    texture.UpdateData(ctx, normal_pixel.data(), static_cast<uint32_t>(normal_pixel.size()));
}

vkfw::VkTexture const* ResolveTextureOrFallback(vkfw::VkContext& ctx,
                                                ave::resource::TextureManager& texture_mgr,
                                                uint32_t texture_id,
                                                vkfw::VkTexture& fallback_texture)
{
    if (texture_id != 0) {
        if (auto const* runtime = texture_mgr.GetTexture(texture_id)) {
            if (runtime->texture && runtime->texture->IsInitialized()) {
                return runtime->texture.get();
            }
        }
    }

    EnsureFallbackWhiteTexture(ctx, fallback_texture);
    return fallback_texture.IsInitialized() ? &fallback_texture : nullptr;
}

vkfw::VkTexture const* ResolveNormalTextureOrFallback(vkfw::VkContext& ctx,
                                                      ave::resource::TextureManager& texture_mgr,
                                                      uint32_t texture_id,
                                                      vkfw::VkTexture& fallback_texture)
{
    if (texture_id != 0) {
        if (auto const* runtime = texture_mgr.GetTexture(texture_id)) {
            if (runtime->texture && runtime->texture->IsInitialized()) {
                return runtime->texture.get();
            }
        }
    }

    EnsureFallbackNormalTexture(ctx, fallback_texture);
    return fallback_texture.IsInitialized() ? &fallback_texture : nullptr;
}

PipelineKey MakePipelineKey(uint32_t shader_id, ave::resource::MeshRuntime const& mesh)
{
    (void)mesh;
    PipelineKey key{};
    key.shader_id = shader_id;
    key.render_state_id = 1;
    key.layout_profile = PipelineLayoutProfile::Empty;
    key.rt_format = 0;
    key.depth_format = 0;
    key.stencil_format = 0;
    key.sample_count = 1;
    key.viewport_width = 0;
    key.viewport_height = 0;
    return key;
}

glm::mat4 BuildShadowViewProjection(PassExecutionView const& view, core::FrameData const* frame)
{
    (void)frame;

    glm::vec3 const scene_center{0.0f, 0.0f, 0.0f};
    float radius = 20.0f;

    for (auto const* renderable : view.renderables) {
        if (!renderable) {
            continue;
        }
        glm::vec3 const position = glm::vec3(renderable->world[3]);
        radius = std::max(radius, glm::length(position - scene_center) + 5.0f);
    }

    glm::vec3 light_direction{0.35f, -1.0f, 0.25f};
    bool has_light_direction = false;
    for (auto const* light : view.lights) {
        if (!light || !light->cast_shadows) {
            continue;
        }
        if (light->type == "directional" || light->type.empty()) {
            if (glm::length(light->direction) > 0.0001f) {
                light_direction = light->direction;
                has_light_direction = true;
                break;
            }
        } else {
            glm::vec3 const to_scene = scene_center - light->position;
            if (glm::length(to_scene) > 0.0001f) {
                light_direction = to_scene;
                has_light_direction = true;
            }
        }
        radius = std::max(radius, light->range);
        break;
    }

    if (!has_light_direction || glm::length(light_direction) < 0.0001f) {
        light_direction = glm::vec3{0.35f, -1.0f, 0.25f};
    }
    light_direction = glm::normalize(light_direction);

    glm::vec3 up = std::abs(glm::dot(light_direction, glm::vec3{0.0f, 1.0f, 0.0f})) > 0.95f
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};

    radius = std::clamp(radius, 10.0f, 100.0f);

    glm::vec3 const eye = scene_center - light_direction * (radius * 2.5f);
    glm::mat4 const light_view = glm::lookAtRH(eye, scene_center, up);
    glm::mat4 const light_projection = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.1f, radius * 6.0f);
    glm::mat4 shadow_view_projection = light_projection * light_view;
    shadow_view_projection[1][1] *= -1.0f;
    return shadow_view_projection;
}

void TransitionImageLayout(vk::CommandBuffer const& command_buffer,
                           vk::Image image,
                           vk::ImageAspectFlags aspect_mask,
                           vk::ImageLayout old_layout,
                           vk::ImageLayout new_layout,
                           vk::AccessFlags src_access_mask,
                           vk::AccessFlags dst_access_mask,
                           vk::PipelineStageFlags src_stage,
                           vk::PipelineStageFlags dst_stage)
{
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access_mask;
    barrier.dstAccessMask = dst_access_mask;
    command_buffer.pipelineBarrier(src_stage, dst_stage, {}, {}, {}, barrier);
}

bool BeginShadowMapRendering(RenderPassContext const& context,
                             vkfw::VkTexture const& shadow_map,
                             uint32_t shadow_map_size,
                             vk::ClearDepthStencilValue const& clear_depth)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{} || !shadow_map.IsInitialized()) {
        return false;
    }
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo depth_attachment{};
            depth_attachment.imageView = shadow_map.View();
            depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            depth_attachment.clearValue.depthStencil = clear_depth;
            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 0;
            rendering_info.pDepthAttachment = &depth_attachment;
            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR depth_attachment{};
            depth_attachment.imageView = shadow_map.View();
            depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
            depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
            depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            depth_attachment.clearValue.depthStencil = clear_depth;
            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 0;
            rendering_info.pDepthAttachment = &depth_attachment;
            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    if (!g_compatibility_shadow_render_pass) {
        vk::AttachmentDescription depth_attachment{};
        depth_attachment.format = shadow_map.Format();
        depth_attachment.samples = vk::SampleCountFlagBits::e1;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        depth_attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depth_attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depth_attachment.initialLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depth_attachment.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        vk::AttachmentReference depth_attachment_ref{};
        depth_attachment_ref.attachment = 0;
        depth_attachment_ref.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.pDepthStencilAttachment = &depth_attachment_ref;

        std::array<vk::SubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo render_pass_info{};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &depth_attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
        render_pass_info.pDependencies = dependencies.data();
        g_compatibility_shadow_render_pass = std::make_unique<vk::raii::RenderPass>(context.vk->Device(), render_pass_info);
    }

    if (g_compatibility_shadow_framebuffer && g_last_shadow_image_view != shadow_map.View()) {
        g_compatibility_shadow_framebuffer.reset();
    }

    if (!g_compatibility_shadow_framebuffer) {
        g_last_shadow_image_view = shadow_map.View();
        vk::ImageView attachment = shadow_map.View();
        vk::FramebufferCreateInfo framebuffer_info{};
        framebuffer_info.renderPass = **g_compatibility_shadow_render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &attachment;
        framebuffer_info.width = shadow_map_size;
        framebuffer_info.height = shadow_map_size;
        framebuffer_info.layers = 1;
        g_compatibility_shadow_framebuffer = std::make_unique<vk::raii::Framebuffer>(context.vk->Device(), framebuffer_info);
    }

    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = **g_compatibility_shadow_render_pass;
    render_pass_begin.framebuffer = **g_compatibility_shadow_framebuffer;
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, vk::Extent2D{shadow_map_size, shadow_map_size}};
    vk::ClearValue clear_value{};
    clear_value.depthStencil = clear_depth;
    render_pass_begin.clearValueCount = 1;
    render_pass_begin.pClearValues = &clear_value;
    context.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    return true;
}

bool BeginDepthOnlyRendering(RenderPassContext const& context,
                             vkfw::VkTexture const& depth_texture,
                             vk::Extent2D extent,
                             vk::ClearDepthStencilValue const& clear_depth)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{} || !depth_texture.IsInitialized()) {
        return false;
    }

    static bool logged_warning = false;
    if (!context.vk->SupportsDynamicRendering()) {
        if (!logged_warning) {
            LOGE("DepthPrepass requires dynamic rendering in the current backend");
            logged_warning = true;
        }
        return false;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (core_dynamic_rendering) {
        vk::ImageView const raw_depth_view = depth_texture.View();
        vk::RenderingAttachmentInfo depth_attachment{};
        depth_attachment.imageView = raw_depth_view;
        depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        depth_attachment.clearValue.depthStencil = clear_depth;
        vk::RenderingInfo rendering_info{};
        rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 0;
        rendering_info.pDepthAttachment = &depth_attachment;
        context.command_buffer.beginRendering(rendering_info);
    } else {
        vk::ImageView const raw_depth_view = depth_texture.View();
        vk::RenderingAttachmentInfoKHR depth_attachment{};
        depth_attachment.imageView = raw_depth_view;
        depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        depth_attachment.clearValue.depthStencil = clear_depth;
        vk::RenderingInfoKHR rendering_info{};
        rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 0;
        rendering_info.pDepthAttachment = &depth_attachment;
        context.command_buffer.beginRenderingKHR(rendering_info);
    }
    return true;
}

void EndShadowMapRendering(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            context.command_buffer.endRendering();
        } else {
            context.command_buffer.endRenderingKHR();
        }
    } else {
        context.command_buffer.endRenderPass();
    }
}

bool BeginSwapchainRendering(RenderPassContext const& context,
                             vk::ClearValue const& clear_value,
                             bool clear_color,
                             vkfw::VkTexture const* depth_texture,
                             bool clear_depth)
{
    if (context.vk == nullptr || context.swapchain == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return false;
    }

    auto const extent = context.swapchain->Extent();
    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;

    if (context.vk->SupportsDynamicRendering()) {
        vk::ClearDepthStencilValue depth_clear_value{1.0f, 0};
        if (core_dynamic_rendering) {
            vk::RenderingAttachmentInfo color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingAttachmentInfo depth_attachment{};
            if (depth_texture && depth_texture->IsInitialized()) {
                depth_attachment.imageView = depth_texture->View();
                depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
                depth_attachment.loadOp = clear_depth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
                depth_attachment.clearValue.depthStencil = depth_clear_value;
            }

            vk::RenderingInfo rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            if (depth_texture && depth_texture->IsInitialized()) {
                rendering_info.pDepthAttachment = &depth_attachment;
            }

            context.command_buffer.beginRendering(rendering_info);
        } else {
            vk::RenderingAttachmentInfoKHR color_attachment{};
            color_attachment.imageView = context.swapchain->ImageView(context.swapchain_image_index);
            color_attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            color_attachment.loadOp = clear_color ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
            color_attachment.clearValue = clear_value;

            vk::RenderingAttachmentInfoKHR depth_attachment{};
            if (depth_texture && depth_texture->IsInitialized()) {
                depth_attachment.imageView = depth_texture->View();
                depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
                depth_attachment.loadOp = clear_depth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
                depth_attachment.clearValue.depthStencil = depth_clear_value;
            }

            vk::RenderingInfoKHR rendering_info{};
            rendering_info.renderArea = vk::Rect2D{{0, 0}, extent};
            rendering_info.layerCount = 1;
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachments = &color_attachment;
            if (depth_texture && depth_texture->IsInitialized()) {
                rendering_info.pDepthAttachment = &depth_attachment;
            }

            context.command_buffer.beginRenderingKHR(rendering_info);
        }
        return true;
    }

    vk::RenderPass compatibility_render_pass = clear_color
        ? context.compatibility_render_pass
        : context.compatibility_load_render_pass;
    vk::Framebuffer compatibility_framebuffer = clear_color
        ? context.compatibility_framebuffer
        : context.compatibility_load_framebuffer;

    if (compatibility_render_pass == vk::RenderPass{} ||
        compatibility_framebuffer == vk::Framebuffer{}) {
        return false;
    }

    std::array<vk::ClearValue, 2> clear_values{};
    clear_values[0] = clear_value;
    clear_values[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo render_pass_begin{};
    render_pass_begin.renderPass = compatibility_render_pass;
    render_pass_begin.framebuffer = compatibility_framebuffer;
    render_pass_begin.renderArea = vk::Rect2D{{0, 0}, extent};
    render_pass_begin.clearValueCount = 2;
    render_pass_begin.pClearValues = clear_values.data();
    context.command_buffer.beginRenderPass(render_pass_begin, vk::SubpassContents::eInline);
    return true;
}

void EndSwapchainRendering(RenderPassContext const& context)
{
    if (context.vk == nullptr || context.command_buffer == vk::CommandBuffer{}) {
        return;
    }

    bool const core_dynamic_rendering =
        context.vk->PhysicalDevice().getProperties().apiVersion >= VK_API_VERSION_1_3;
    if (context.vk->SupportsDynamicRendering()) {
        if (core_dynamic_rendering) {
            context.command_buffer.endRendering();
        } else {
            context.command_buffer.endRenderingKHR();
        }
    } else {
        context.command_buffer.endRenderPass();
    }
}

void AppendUiQuad(std::vector<ave::render::UiVertex>& vertices,
                  std::vector<uint32_t>& indices,
                  core::FrameUiData const& item,
                  uint32_t texture_index,
                  float aspect_ratio)
{
    glm::vec2 const half_size = item.size * 0.5f;
    glm::vec3 const center = ClampUiPosition(item.position, item.depth);
    float const left = -half_size.x;
    float const right =
        item.kind == core::FrameUiData::Kind::ProgressBarFill
            ? left + item.size.x * std::clamp(item.fill_amount, 0.0f, 1.0f)
            : half_size.x;

    uint32_t const base_vertex = static_cast<uint32_t>(vertices.size());

    auto make_vertex = [&](float x, float y, float u, float v) {
        ave::render::UiVertex vertex{};
        vertex.position = glm::vec2{
            std::clamp(center.x + x / aspect_ratio, -1.0f, 1.0f),
            std::clamp(center.y + y, -1.0f, 1.0f),
        };
        vertex.uv = glm::vec2{u, v};
        vertex.color = item.color;
        vertex.texture_index = texture_index;
        return vertex;
    };

    vertices.push_back(make_vertex(left, -half_size.y, item.uv_min.x, item.uv_max.y));
    vertices.push_back(make_vertex(right, -half_size.y, item.uv_max.x, item.uv_max.y));
    vertices.push_back(make_vertex(right, half_size.y, item.uv_max.x, item.uv_min.y));
    vertices.push_back(make_vertex(left, half_size.y, item.uv_min.x, item.uv_min.y));

    indices.push_back(base_vertex + 0);
    indices.push_back(base_vertex + 1);
    indices.push_back(base_vertex + 2);
    indices.push_back(base_vertex + 0);
    indices.push_back(base_vertex + 2);
    indices.push_back(base_vertex + 3);
}

void EnsureSharedEnvironmentMaps(vkfw::VkContext& ctx,
                                 resource::ResourceSystem* resources,
                                 glm::vec4 const& clear_color,
                                 glm::vec3 const& ambient_color)
{
    if (resources != nullptr && !g_maskonaive_source.ready) {
        auto& texture_mgr = resources->GetTextureManager();
        if (LoadMaskonaiveCubemapSource(texture_mgr, g_maskonaive_source)) {
        } else {
            LOGW("Falling back to procedural skybox/environment");
        }
    }

    bool const has_cubemap_source = g_maskonaive_source.ready;
    bool const needs_rebuild =
        !g_shared_environment_maps.ready ||
        g_shared_environment_maps.last_clear_color != clear_color ||
        g_shared_environment_maps.last_ambient_color != ambient_color ||
        g_shared_environment_maps.last_use_cubemap_source != has_cubemap_source;
    if (!needs_rebuild && g_shared_environment_maps.environment_cubemap.IsInitialized() &&
        g_shared_environment_maps.irradiance_cubemap.IsInitialized() &&
        g_shared_environment_maps.prefilter_cubemap.IsInitialized() &&
        g_shared_environment_maps.brdf_lut.IsInitialized()) {
        return;
    }

    if (g_shared_environment_maps.environment_cubemap.IsInitialized()) {
        g_shared_environment_maps.environment_cubemap.Shutdown(ctx);
    }
    if (g_shared_environment_maps.irradiance_cubemap.IsInitialized()) {
        g_shared_environment_maps.irradiance_cubemap.Shutdown(ctx);
    }
    if (g_shared_environment_maps.prefilter_cubemap.IsInitialized()) {
        g_shared_environment_maps.prefilter_cubemap.Shutdown(ctx);
    }
    if (g_shared_environment_maps.brdf_lut.IsInitialized()) {
        g_shared_environment_maps.brdf_lut.Shutdown(ctx);
    }

    constexpr uint32_t kIrradianceSize = 32;
    constexpr uint32_t kPrefilterSize = 64;
    constexpr uint32_t kBrdfLutSize = 128;
    constexpr uint32_t kPrefilterMipLevels = 7;

    uint32_t const kEnvironmentSize = has_cubemap_source ? g_maskonaive_source.faces[0].width : 64u;

    vkfw::TextureInfo env_info{};
    env_info.width = kEnvironmentSize;
    env_info.height = kEnvironmentSize;
    env_info.mip_levels = 1;
    env_info.array_layers = 6;
    env_info.cube_map = true;
    env_info.format = vkfw::TextureFormat::R32G32B32A32_SFLOAT;
    env_info.usage = static_cast<vkfw::TextureUsage>(
        static_cast<uint32_t>(vkfw::TextureUsage::Sampled) |
        static_cast<uint32_t>(vkfw::TextureUsage::TransferDst));
    env_info.mipmap = false;
    if (!g_shared_environment_maps.environment_cubemap.Init(ctx, env_info)) {
        LOGE("Failed to initialize environment cubemap");
        return;
    }

    vkfw::TextureInfo irradiance_info = env_info;
    irradiance_info.width = kIrradianceSize;
    irradiance_info.height = kIrradianceSize;
    if (!g_shared_environment_maps.irradiance_cubemap.Init(ctx, irradiance_info)) {
        LOGE("Failed to initialize irradiance cubemap");
        return;
    }

    vkfw::TextureInfo prefilter_info = env_info;
    prefilter_info.width = kPrefilterSize;
    prefilter_info.height = kPrefilterSize;
    prefilter_info.mip_levels = kPrefilterMipLevels;
    if (!g_shared_environment_maps.prefilter_cubemap.Init(ctx, prefilter_info)) {
        LOGE("Failed to initialize prefilter cubemap");
        return;
    }

    vkfw::TextureInfo brdf_info{};
    brdf_info.width = kBrdfLutSize;
    brdf_info.height = kBrdfLutSize;
    brdf_info.mip_levels = 1;
    brdf_info.format = vkfw::TextureFormat::R32G32B32A32_SFLOAT;
    brdf_info.usage = static_cast<vkfw::TextureUsage>(
        static_cast<uint32_t>(vkfw::TextureUsage::Sampled) |
        static_cast<uint32_t>(vkfw::TextureUsage::TransferDst));
    brdf_info.mipmap = false;
    if (!g_shared_environment_maps.brdf_lut.Init(ctx, brdf_info)) {
        LOGE("Failed to initialize BRDF LUT texture");
        return;
    }

    glm::vec3 const clear_rgb{clear_color.x, clear_color.y, clear_color.z};
    CpuCubemapSource const* const source = has_cubemap_source ? &g_maskonaive_source : nullptr;

    for (uint32_t face = 0; face < 6; ++face) {
        std::vector<glm::vec4> env_pixels;
        if (has_cubemap_source) {
            env_pixels = g_maskonaive_source.faces[face].pixels;
        } else {
            GenerateProceduralCubemapFace(env_pixels, kEnvironmentSize, face, clear_rgb, ambient_color);
        }
        UploadCubemapFace(ctx, g_shared_environment_maps.environment_cubemap, env_pixels, 0, face);
    }

        for (uint32_t face = 0; face < 6; ++face) {
            std::vector<glm::vec4> irradiance_pixels(static_cast<size_t>(kIrradianceSize) * kIrradianceSize);
        for (uint32_t y = 0; y < kIrradianceSize; ++y) {
            for (uint32_t x = 0; x < kIrradianceSize; ++x) {
                float const u = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(kIrradianceSize)) - 1.0f;
                float const v = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(kIrradianceSize)) - 1.0f;
                glm::vec3 const normal = FaceDirection(face, u, v);
                glm::vec3 const color = IntegrateDiffuseIrradiance(normal, source, clear_rgb, ambient_color);
                    irradiance_pixels[static_cast<size_t>(y) * kIrradianceSize + x] = glm::vec4{color, 1.0f};
                }
            }
        UploadCubemapFace(ctx, g_shared_environment_maps.irradiance_cubemap, irradiance_pixels, 0, face);
    }

    for (uint32_t mip = 0; mip < kPrefilterMipLevels; ++mip) {
        uint32_t const face_size = std::max(1u, kPrefilterSize >> mip);
        float const roughness = kPrefilterMipLevels > 1
            ? static_cast<float>(mip) / static_cast<float>(kPrefilterMipLevels - 1)
            : 0.0f;
        for (uint32_t face = 0; face < 6; ++face) {
            std::vector<glm::vec4> face_pixels(static_cast<size_t>(face_size) * face_size);
            for (uint32_t y = 0; y < face_size; ++y) {
                for (uint32_t x = 0; x < face_size; ++x) {
                    float const u = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(face_size)) - 1.0f;
                    float const v = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(face_size)) - 1.0f;
                    glm::vec3 const normal = FaceDirection(face, u, v);
                    glm::vec3 const color = IntegratePrefilteredEnvironment(normal, roughness, source, clear_rgb, ambient_color);
                    face_pixels[static_cast<size_t>(y) * face_size + x] = glm::vec4{color, 1.0f};
                }
            }
            UploadCubemapFace(ctx, g_shared_environment_maps.prefilter_cubemap, face_pixels, mip, face);
        }
    }

    std::vector<glm::vec4> brdf_pixels(static_cast<size_t>(kBrdfLutSize) * kBrdfLutSize);
    for (uint32_t y = 0; y < kBrdfLutSize; ++y) {
        float const roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(kBrdfLutSize);
        for (uint32_t x = 0; x < kBrdfLutSize; ++x) {
            float const n_dot_v = (static_cast<float>(x) + 0.5f) / static_cast<float>(kBrdfLutSize);
            glm::vec2 const ab = IntegrateBrdf(n_dot_v, roughness);
            brdf_pixels[static_cast<size_t>(y) * kBrdfLutSize + x] = glm::vec4{ab, 0.0f, 1.0f};
        }
    }
    g_shared_environment_maps.brdf_lut.UpdateData(ctx, brdf_pixels.data(), static_cast<uint32_t>(brdf_pixels.size() * sizeof(glm::vec4)));

    g_shared_environment_maps.last_clear_color = clear_color;
    g_shared_environment_maps.last_ambient_color = ambient_color;
    g_shared_environment_maps.last_use_cubemap_source = has_cubemap_source;
    g_shared_environment_maps.ready = true;
}

} // namespace ave::render::detail
