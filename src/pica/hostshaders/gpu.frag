const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;

out vec4 fragclr;

uniform sampler2D tex0;
uniform bool tex0enable;

void main() {
    if (tex0enable) {
        fragclr = texture(tex0, texcoord0);
    } else {
        fragclr = color;
    }
}

)";
