#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;
layout(location = 3) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D u_ShadowMap;

layout(set = 1, binding = 0) uniform MaterialUbo {
    vec4 base_color;
    // x = metallic, y = roughness, z = receivesShadow, w = unused
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

vec4 aveUserColor;

// __AVE_USER_FRAGMENT__

float AveComputeShadow(vec4 shadowCoord) {
    if (material.params.z < 0.5) {
        return 1.0;
    }

    vec3 projCoord = shadowCoord.xyz / shadowCoord.w;
    projCoord.xy = projCoord.xy * 0.5 + 0.5;
    float currentDepth = projCoord.z;

    if (shadowCoord.w <= 0.0 ||
        currentDepth < 0.0 || currentDepth > 1.0 ||
        projCoord.x < 0.0 || projCoord.x > 1.0 ||
        projCoord.y < 0.0 || projCoord.y > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(u_ShadowMap, projCoord.xy).r;
    float bias = 0.03;
    return (currentDepth - bias) <= closestDepth ? 1.0 : 0.35;
}

void main() {
    aveUserColor = vec4(1.0);
    AveUserFragmentMain();
    float shadowFactor = AveComputeShadow(inShadowCoord);
    outColor = vec4(aveUserColor.rgb * shadowFactor, aveUserColor.a);
}
