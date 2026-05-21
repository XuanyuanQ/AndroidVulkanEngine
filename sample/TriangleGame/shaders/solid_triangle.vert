#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
} frame;

void main() {
    // mat4 testVP = mat4(
    //     vec4(0.974279,  0.000000,  0.000000,  0.000000),  // 第一列
    //     vec4(0.000000,  1.732051,  0.000000,  0.000000),  // 第二列
    //     vec4(0.000000,  0.000000, -1.000200, -1.000000),  // 第三列
    //     vec4(-9.565559, 0.000000, 13.476130, 13.673416)   // 第四列（平移列）
    // );
    // mat4 testVP = mat4(
    //     vec4(1.0, 0.0, 0.0, 0.0),  // 第一列
    //     vec4(0.0, 1.0, 0.0, 0.0),  // 第二列
    //     vec4(0.0, 0.0, 1.0, 0.0),  // 第三列
    //     vec4(0.0, 0.0, 0.0, 1.0)   // 第四列
    // );
    gl_Position = frame.view_projection * vec4(inPosition, 1.0);
    vColor = inColor;
}
