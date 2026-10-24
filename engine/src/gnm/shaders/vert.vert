#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inTex;
layout(location = 2) in vec4 inCol;

layout(location = 0) out vec2 fragTex;
layout(location = 1) out vec4 fragCol;

void main() {
    gl_Position = vec4(inPos, 1.0);
    fragTex = inTex;
    fragCol = inCol;
}
