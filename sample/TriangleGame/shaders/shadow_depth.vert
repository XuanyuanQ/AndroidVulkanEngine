#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
} frame;

void main() {
    gl_Position = frame.view_projection * vec4(inPosition, 1.0);
}
