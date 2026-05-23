#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;
layout(location = 3) in vec4 inShadowCoord; // 从顶点着色器接收的阴影坐标

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2DShadow u_ShadowMap;
layout(set = 1, binding = 0) uniform MaterialUbo {
    vec4 base_color;
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

void main() {
    vec3 projCoord = inShadowCoord.xyz / inShadowCoord.w;
    projCoord.xy = projCoord.xy * 0.5 + 0.5;
    float bias = 0.005; 
    projCoord.z -= bias;
    
    float shadowFactor = 1.0;
    // if (projCoord.z > 0.0 && projCoord.z < 1.0) {
    //     // 采样硬件阴影图（内部自带 PCF 过滤）
    //     shadowFactor = texture(u_ShadowMap, projCoord);
    // }
    vec4 sampled = texture(baseColorTexture, vTexcoord0);
    outColor = sampled*shadowFactor;
}
