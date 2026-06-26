#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexcoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in uint inTextureIndex;

layout(location = 0) out vec2 vTexcoord0;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0);
    vTexcoord0 = inTexcoord;
    vColor = inColor;
}
