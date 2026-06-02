#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec2 inTexcoord0;
layout(location = 4) in vec2 inTexcoord1;
layout(location = 5) in vec4 inColor;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexcoord0;
layout(location = 2) out vec3 vWorldNormal;
layout(location = 3) out vec4 outShadowCoord;
layout(location = 4) out vec3 vWorldPosition;
layout(location = 5) out vec4 vWorldTangent;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
    mat4 shadowViewProj;
    mat4 view;
    mat4 projection;
} frame;

layout(push_constant) uniform ObjectPushConstants {
    mat4 world;
} object_pc;

void main() {
    vec4 worldPosition = object_pc.world * vec4(inPosition, 1.0);
    gl_Position = frame.view_projection * worldPosition;
    vColor = inColor;
    vTexcoord0 = inTexcoord0;
    mat3 normalMatrix = transpose(inverse(mat3(object_pc.world)));
    vWorldNormal = normalize(normalMatrix * normal);
    vec3 worldTangent = normalize(normalMatrix * tangent.xyz);
    worldTangent = normalize(worldTangent - vWorldNormal * dot(vWorldNormal, worldTangent));
    vWorldTangent = vec4(worldTangent, tangent.w);
    vWorldPosition = worldPosition.xyz;
    outShadowCoord = frame.shadowViewProj * worldPosition;
}
