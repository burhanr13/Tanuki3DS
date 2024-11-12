const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;

out vec4 fragclr;

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;

layout (std140) uniform UberUniforms {
    struct {
        struct {
            int src0;
            int op0;
            int src1;
            int op1;
            int src2;
            int op2;
            int combiner;
            float scale;
        } rgb, a;
        vec4 color;
    } tev[6];

    bool alphatest;
    int alphafunc;
    float alpharef;
};

vec4 tev_source(int src, int op, int i) {
    vec4 v = vec4(1);
    switch (src) {
        case 0: v = color; break;
        case 3: v = texture(tex0, texcoord0); break;
        case 4: v = texture(tex1, texcoord1); break;
        case 5: v = texture(tex2, texcoord2); break;
        case 14: v = tev[i].color;
        case 15: v = fragclr;
    }
    switch (op) {
        case 0: return v;
        case 1: return 1 - v;
        case 2: return v.aaaa;
        case 3: return 1 - v.aaaa;
        case 4: return v.rrrr;
        case 5: return 1 - v.rrrr;
        case 8: return v.gggg;
        case 9: return 1 - v.gggg;
        case 12: return v.bbbb;
        case 13: return 1 - v.bbbb;
        default: return v;
    }
}

vec4 tev_combine_rgb(int i) {
#define SRC(_i) tev_source(tev[i].rgb.src##_i, tev[i].rgb.op##_i, i)
    switch (tev[i].rgb.combiner) {
        case 0: return SRC(0);
        case 1: return SRC(0) * SRC(1);
        case 2: return SRC(0) + SRC(1);
        case 3: return SRC(0) + SRC(1) - 0.5;
        case 4: return mix(SRC(1), SRC(0), SRC(2));
        case 5: return SRC(0) - SRC(1);
        case 6: return 4 * vec4(vec3(dot(SRC(0).rgb - 0.5, SRC(1).rgb - 0.5)), 0.25);
        case 7: return 4 * vec4(dot(SRC(0).rgb - 0.5, SRC(1).rgb - 0.5));
        case 8: return SRC(0) * SRC(1) + SRC(2);
        case 9: return (SRC(0) + SRC(1)) * SRC(2);
        default: return SRC(0);
    }
#undef SRC
}

vec4 tev_combine_alpha(int i) {
#define SRC(_i) tev_source(tev[i].a.src##_i, tev[i].a.op##_i, i)
    switch (tev[i].rgb.combiner) {
        case 0: return SRC(0);
        case 1: return SRC(0) * SRC(1);
        case 2: return SRC(0) + SRC(1);
        case 3: return SRC(0) + SRC(1) - 0.5;
        case 4: return mix(SRC(1), SRC(0), SRC(2));
        case 5: return SRC(0) - SRC(1);
        case 6: return 4 * vec4(vec3(dot(SRC(0).rgb - 0.5, SRC(1).rgb - 0.5)), 0.25);
        case 7: return 4 * vec4(dot(SRC(0).rgb - 0.5, SRC(1).rgb - 0.5));
        case 8: return SRC(0) * SRC(1) + SRC(2);
        case 9: return (SRC(0) + SRC(1)) * SRC(2);
        default: return SRC(0);
    }
#undef SRC
}

bool run_alphatest() {
    if (alphatest) {
        switch (alphafunc) {
            case 0: return false;
            case 1: return true;
            case 2: return fragclr.a == alpharef;
            case 3: return fragclr.a != alpharef;
            case 4: return fragclr.a < alpharef;
            case 5: return fragclr.a <= alpharef;
            case 6: return fragclr.a > alpharef;
            case 7: return fragclr.a >= alpharef;
        }
    }
    return true;
}

void main() {
    for (int i = 0; i < 6; i++) {
        fragclr = vec4(
            clamp(tev[i].rgb.scale * tev_combine_rgb(i).rgb, 0, 1),
            clamp(tev[i].a.scale * tev_combine_alpha(i).a, 0, 1)
        );
    }

    if (!run_alphatest()) discard;
}

)";
