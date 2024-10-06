const char* gpuvertsource = R"(
#version 410 core

layout (location=0) in vec4 pos;
layout (location=1) in vec4 color;
out vec4 outclr;

void main() {
    gl_Position = pos;
    outclr = color;
}

)";