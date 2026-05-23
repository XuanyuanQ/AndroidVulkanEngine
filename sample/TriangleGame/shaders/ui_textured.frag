#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uiTexture;

void main() {
    outColor = texture(uiTexture, vTexcoord0) * vColor;
}
