#version 450

layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 2) uniform samplerCube environmentMap;

void main() {
    vec3 color = texture(environmentMap, normalize(vDirection)).rgb;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
