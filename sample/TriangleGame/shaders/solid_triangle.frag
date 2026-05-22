#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform MaterialUbo {
    vec4 base_color;
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;

void main() {
    vec4 sampled = texture(baseColorTexture, vTexcoord0);
    outColor = sampled;
}
