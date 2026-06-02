#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 vDirection;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
    mat4 shadowViewProj;
    vec4 camera_position;
    vec4 light_position_range;
    vec4 light_direction_type;
    vec4 light_color_intensity;
    vec4 ambient_color;
    vec4 clear_color;
    mat4 view;
    mat4 projection;
} frame;

layout(push_constant) uniform ObjectPushConstants {
    mat4 world;
} object_pc;

void main() {
    vec3 cubePosition = inPosition * 2.0;
    vDirection = cubePosition;
    vec4 viewPosition = frame.projection * mat4(mat3(frame.view)) * vec4(cubePosition, 1.0);
    gl_Position = viewPosition.xyww;
}
