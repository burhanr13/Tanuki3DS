#version 330 core

in vec2 texcoord;
out vec4 fragclr;

uniform sampler2D screen;

void main() {
    ivec2 dims = textureSize(screen, 0);
    vec2 tc_scaled = texcoord * dims;
    vec2 scale = ceil(1 / fwidth(tc_scaled));
    vec2 f = fract(tc_scaled);
    f = clamp(f * scale, 0, 0.5) + clamp((f - 1) * scale + 0.5, 0, 0.5);
    tc_scaled = floor(tc_scaled) + f;
    fragclr = texture(screen, tc_scaled / dims);
}
