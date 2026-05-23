#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;
layout(location = 3) in vec4 inShadowCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D u_ShadowMap;
layout(set = 1, binding = 0) uniform MaterialUbo {
    vec4 base_color;
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

void main() {
    vec3 projCoord = inShadowCoord.xyz / inShadowCoord.w;
    projCoord.xy = projCoord.xy * 0.5 + 0.5;
    float currentDepth = projCoord.z;

    float shadowFactor = 1.0;
    if (inShadowCoord.w > 0.0 &&
        currentDepth >= 0.0 && currentDepth <= 1.0 &&
        projCoord.x >= 0.0 && projCoord.x <= 1.0 &&
        projCoord.y >= 0.0 && projCoord.y <= 1.0) {
        float closestDepth = texture(u_ShadowMap, projCoord.xy).r;
        float bias = 0.0025;
        shadowFactor = (currentDepth - bias) <= closestDepth ? 1.0 : 0.35;
    }

    vec4 sampled = texture(baseColorTexture, vTexcoord0);
    vec4 litColor = sampled * material.base_color * vColor;
    outColor = vec4(litColor.rgb * shadowFactor, litColor.a);
}
