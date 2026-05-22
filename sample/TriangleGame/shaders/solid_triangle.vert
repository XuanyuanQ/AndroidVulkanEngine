#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec2 inTexcoord0;
layout(location = 4) in vec2 inTexcoord1;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord0;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
} frame;

void main() {
    gl_Position = frame.view_projection * vec4(inPosition, 1.0);
    vColor = inColor;
    vTexcoord0 = inTexcoord0;
}
