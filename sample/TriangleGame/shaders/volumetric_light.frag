#version 450

layout(location = 0) in vec2 vTexcoord0;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D shadowMap;
layout(set = 0, binding = 2) uniform sampler2D sceneDepth;

layout(set = 0, binding = 0) uniform VolumetricLight {
    mat4 inverseViewProjection;
    mat4 shadowViewProjection;
    vec4 cameraPositionNear;    // xyz: camera position, w: near plane
    vec4 lightPositionRange;    // xyz: point light position, w: range
    vec4 lightDirectionType;    // xyz: light direction, w: 0 directional, 1 point
    vec4 lightScreenDensity;    // xy: light position in screen UV, z: media density
    vec4 lightColorIntensity;   // rgb: light color, a: effect intensity
    vec4 params;                // x: decay, y: exposure, z: weight
} volumetric;

vec3 ReconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = volumetric.inverseViewProjection * clip;
    return world.xyz / max(world.w, 0.0001);
}

float ShadowVisibility(vec3 world_pos) {
    vec4 shadow_coord = volumetric.shadowViewProjection * vec4(world_pos, 1.0);
    if (shadow_coord.w <= 0.0001) {
        return 1.0;
    }

    vec3 proj = shadow_coord.xyz / shadow_coord.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0 ||
        proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }

    float closest = texture(shadowMap, proj.xy).r;
    float bias = 0.025;
    return (proj.z - bias) <= closest ? 1.0 : 0.0;
}

float LightAttenuation(vec3 world_pos) {
    if (volumetric.lightDirectionType.w < 0.5) {
        return 1.0;
    }

    float dist_to_light = length(volumetric.lightPositionRange.xyz - world_pos);
    float range = max(volumetric.lightPositionRange.w, 0.001);
    float normalized = clamp(dist_to_light / range, 0.0, 1.0);
    return (1.0 - normalized) * (1.0 - normalized);
}

void main() {
    vec2 uv = vTexcoord0;
    float depth = texture(sceneDepth, uv).r;
    if (depth <= 0.0001) {
        discard;
    }

    vec3 camera_pos = volumetric.cameraPositionNear.xyz;
    vec3 surface_pos = ReconstructWorldPosition(uv, depth);
    vec3 ray = surface_pos - camera_pos;
    float ray_length = length(ray);
    if (ray_length < 0.001) {
        discard;
    }

    vec3 ray_dir = ray / ray_length;
    float max_distance = min(ray_length, 35.0);
    float density = max(volumetric.lightScreenDensity.z, 0.001);
    float decay = clamp(volumetric.params.x, 0.0, 0.999);
    float exposure = max(volumetric.params.y, 0.0);
    float weight = max(volumetric.params.z, 0.0);

    vec2 to_light_screen = volumetric.lightScreenDensity.xy - uv;
    float screen_distance = length(to_light_screen);
    float wide_beam = smoothstep(1.10, 0.0, screen_distance);
    float core_beam = pow(smoothstep(0.62, 0.0, screen_distance), 2.2);
    float screen_beam = 0.08 + wide_beam * 0.62 + core_beam * 2.65;

    // Stable screen-space shaft term: sky/empty depth contributes light, geometry blocks it.
    float screen_scattering = 0.0;
    float screen_decay = 1.0;
    const int SCREEN_STEP_COUNT = 48;
    for (int i = 0; i < SCREEN_STEP_COUNT; ++i) {
        float t = (float(i) + 0.5) / float(SCREEN_STEP_COUNT);
        vec2 sample_uv = mix(uv, volumetric.lightScreenDensity.xy, t);
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0) {
            screen_decay *= decay;
            continue;
        }

        float sample_depth = texture(sceneDepth, sample_uv).r;
        float open_air = smoothstep(0.965, 1.0, sample_depth);
        screen_scattering += open_air * screen_decay;
        screen_decay *= decay;
    }
    screen_scattering /= float(SCREEN_STEP_COUNT);

    float scattering = 0.0;
    float current_decay = 1.0;
    const int STEP_COUNT = 32;
    for (int i = 0; i < STEP_COUNT; ++i) {
        float t = (float(i) + 0.5) / float(STEP_COUNT);
        float distance_on_ray = t * max_distance;
        vec3 sample_pos = camera_pos + ray_dir * distance_on_ray;

        float shadow = mix(0.35, 1.0, ShadowVisibility(sample_pos));
        float attenuation = LightAttenuation(sample_pos);
        float air_falloff = exp(-density * t * 1.8);
        scattering += shadow * attenuation * air_falloff * current_decay;
        current_decay *= decay;
    }

    scattering /= float(STEP_COUNT);
    float base_air_scattering = 0.11;
    float combined_scattering = max(scattering * 1.20, base_air_scattering + screen_scattering * 0.72);
    float alpha = clamp(combined_scattering * exposure * weight * screen_beam * volumetric.lightColorIntensity.a, 0.0, 0.72);
    vec3 color = volumetric.lightColorIntensity.rgb;
    outColor = vec4(color * alpha, alpha) * vColor;
}
