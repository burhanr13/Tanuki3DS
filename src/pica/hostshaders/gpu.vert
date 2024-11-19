const char* gpuvertsource = R"(
#version 410 core

layout (location=0) in vec4 a_pos;
layout (location=1) in vec4 a_color;
layout (location=2) in vec2 a_texcoord0;
layout (location=3) in vec2 a_texcoord1;
layout (location=4) in vec2 a_texcoord2;
layout (location=5) in vec4 a_normquat;
layout (location=6) in vec3 a_view;

out vec4 color;
out vec2 texcoord0;
out vec2 texcoord1;
out vec2 texcoord2;
out vec4 normquat;
out vec3 view;

void main() {
    gl_Position = a_pos;
    
    color = a_color;
    texcoord0 = a_texcoord0;
    texcoord1 = a_texcoord1;
    texcoord2 = a_texcoord2;
    normquat = a_normquat;
    view = a_view;
}

)";
