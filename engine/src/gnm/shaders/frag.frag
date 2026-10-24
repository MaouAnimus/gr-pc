#version 450
layout(location = 0) in vec2 fragTex;
layout(location = 1) in vec4 fragCol;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragCol;
}
