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

vec4 cur_color = vec4(1);

vec4 tev_source(int src, int i) {
    vec4 v = vec4(1);
    switch (src) {
        case 0: return color; break;
        case 3: return texture(tex0, texcoord0); break;
        case 4: return texture(tex1, texcoord1); break;
        case 5: return texture(tex2, texcoord2); break;
        case 14: return tev[i].color;
        case 15: return cur_color;
    }
}

vec3 tev_operand_rgb(vec4 v, int op) {
    switch (op) {
        case 0: return v.rgb;
        case 1: return 1 - v.rgb;
        case 2: return vec3(v.a);
        case 3: return vec3(1 - v.a);
        case 4: return vec3(v.r);
        case 5: return vec3(1 - v.r);
        case 8: return vec3(v.g);
        case 9: return vec3(1 - v.g);
        case 12: return vec3(v.b);
        case 13: return vec3(1 - v.b);
        default: return v.rgb;
    }
}

float tev_operand_alpha(vec4 v, int op) {
    switch (op) {
        case 0: return v.a;
        case 1: return 1 - v.a;
        case 2: return v.r;
        case 3: return 1 - v.r;
        case 4: return v.g;
        case 5: return 1 - v.g;
        case 6: return v.b;
        case 7: return 1 - v.b;
        default: return v.a;
    }
}

vec3 tev_combine_rgb(int i) {
#define SRC(_i) tev_operand_rgb(tev_source(tev[i].rgb.src##_i, i), tev[i].rgb.op##_i)
    switch (tev[i].rgb.combiner) {
        case 0: return SRC(0);
        case 1: return SRC(0) * SRC(1);
        case 2: return SRC(0) + SRC(1);
        case 3: return SRC(0) + SRC(1) - 0.5;
        case 4: return mix(SRC(1), SRC(0), SRC(2));
        case 5: return SRC(0) - SRC(1);
        case 6:
        case 7: return vec3(4 * dot(SRC(0) - 0.5, SRC(1) - 0.5));
        case 8: return SRC(0) * SRC(1) + SRC(2);
        case 9: return (SRC(0) + SRC(1)) * SRC(2);
        default: return SRC(0);
    }
#undef SRC
}

float tev_combine_alpha(int i) {
#define SRC(_i) tev_operand_alpha(tev_source(tev[i].a.src##_i, i), tev[i].a.op##_i)
    switch (tev[i].a.combiner) {
        case 0: return SRC(0);
        case 1: return SRC(0) * SRC(1);
        case 2: return SRC(0) + SRC(1);
        case 3: return SRC(0) + SRC(1) - 0.5;
        case 4: return mix(SRC(1), SRC(0), SRC(2));
        case 5: return SRC(0) - SRC(1);
        case 6: 
        case 7: return 4 * (SRC(0) - 0.5) * (SRC(1) - 0.5);
        case 8: return SRC(0) * SRC(1) + SRC(2);
        case 9: return (SRC(0) + SRC(1)) * SRC(2);
        default: return SRC(0);
    }
#undef SRC
}

bool run_alphatest() {
    switch (alphafunc) {
        case 0: return false;
        case 1: return true;
        case 2: return fragclr.a == alpharef;
        case 3: return fragclr.a != alpharef;
        case 4: return fragclr.a < alpharef;
        case 5: return fragclr.a <= alpharef;
        case 6: return fragclr.a > alpharef;
        case 7: return fragclr.a >= alpharef;
        default: return true;
    }
}

void main() {
    for (int i = 0; i < 6; i++) {
        vec4 res;
        res.rgb = tev_combine_rgb(i);
        if (tev[i].rgb.combiner == 7) {
            res.a = res.r;
        } else {
            res.a = tev_combine_alpha(i);
        }
        res.rgb *= tev[i].rgb.scale;
        res.a *= tev[i].a.scale;
        cur_color = clamp(res, 0, 1);
    }

    fragclr = cur_color;

    if (alphatest && !run_alphatest()) discard;

}

)";
