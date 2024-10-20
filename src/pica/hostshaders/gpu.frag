const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;

out vec4 fragclr;

void main() {
    fragclr = color;
}

)";
