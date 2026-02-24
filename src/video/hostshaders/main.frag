#version 330 core

in vec2 texcoord;
out vec4 fragclr;

uniform sampler2D screen;

void main() {
    fragclr = texture(screen, texcoord);
}
