const char* gpuvertsource = R"(
#version 410 core

layout (location=0) in vec4 a_pos;
layout (location=1) in vec4 a_color;
layout (location=2) in vec2 a_texcoord0;

out vec4 color;
out vec2 texcoord0;

void main() {
    gl_Position = a_pos;
    color = a_color;
    texcoord0 = a_texcoord0;
}

)";