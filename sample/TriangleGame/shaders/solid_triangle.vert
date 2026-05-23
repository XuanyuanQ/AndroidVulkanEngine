#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec2 inTexcoord0;
layout(location = 4) in vec2 inTexcoord1;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord0;
layout(location = 3) out vec4 outShadowCoord;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
    mat4 shadowViewProj;
} frame;

layout(push_constant) uniform ObjectPushConstants {
    mat4 world;
} object_pc;

void main() {
    vec4 worldPosition = object_pc.world * vec4(inPosition, 1.0);
    gl_Position = frame.view_projection * worldPosition;
    vColor = inColor;
    vTexcoord0 = inTexcoord0;
    outShadowCoord = frame.shadowViewProj * worldPosition;
}
