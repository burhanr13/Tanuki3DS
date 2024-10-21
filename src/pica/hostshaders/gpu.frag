const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;

out vec4 fragclr;

uniform sampler2D tex0;
uniform bool tex0enable;

void main() {
    if (tex0enable) {
        fragclr = color * texture(tex0, texcoord0);
        // if (fragclr == vec4(0)) {
        //     fragclr = color;
        // }
    } else {
        fragclr = color;
    }
}

)";
