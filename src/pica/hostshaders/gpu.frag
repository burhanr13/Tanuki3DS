const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;

out vec4 fragclr;

uniform sampler2D tex0;
uniform bool tex0enable;

uniform bool alphatest;
uniform int alphafunc;
uniform float alpharef;

void main() {
    if (tex0enable) {
        fragclr = texture(tex0, texcoord0);
    } else {
        fragclr = color;
    }

    if (alphatest) {
        switch (alphafunc) {
            case 0:
                discard;
            case 1:
                break;
            case 2:
                if (fragclr.a != alpharef) discard;
                break;
            case 3:
                if (fragclr.a == alpharef) discard;
                break;
            case 4:
                if (fragclr.a >= alpharef) discard;
                break;
            case 5:
                if (fragclr.a > alpharef) discard;
                break;
            case 6:
                if (fragclr.a <= alpharef) discard;
                break;
            case 7:
                if (fragclr.a < alpharef) discard;
                break;
        }
    }
}

)";
