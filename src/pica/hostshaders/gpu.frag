const char* gpufragsource = R"(
#version 410 core

in vec4 color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;
in vec4 normquat;
in vec3 view;

out vec4 fragclr;

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;

struct TevControl {
    int src0;
    int op0;
    int src1;
    int op1;
    int src2;
    int op2;
    int combiner;
    float scale;
};

struct Tev {
    TevControl rgb;
    TevControl a;
    vec4 color;
};

struct Light {
    vec3 specular0;
    vec3 specular1;
    vec3 diffuse;
    vec3 ambient;
    vec3 dir;
};

layout (std140) uniform UberUniforms {
    Tev tev[6];
    vec4 tev_buffer_color;

    int tev_update_rgb;
    int tev_update_alpha;
    bool tex2coord;

    Light light[8];
    vec4 ambient_color;
    int numlights;

    bool alphatest;
    int alphafunc;
    float alpharef;
};

vec4 tev_srcs[16];

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
#define SRC(n) tev_operand_rgb(tev_srcs[tev[i].rgb.src##n], tev[i].rgb.op##n)
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
#define SRC(n) tev_operand_alpha(tev_srcs[tev[i].a.src##n], tev[i].a.op##n)
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

vec4 quatmul(vec4 p, vec4 q) {
    return vec4(p.x - dot(p.yzw, q.yzw),
                cross(p.yzw, q.yzw) +
                p.x * q.yzw + q.x * p.yzw);
}

vec3 quatrot(vec4 q, vec3 v) {
    return quatmul(quatmul(q, vec4(0, v)), vec4(q.x, -q.yzw)).yzw;
}

void calc_lighting(out vec4 primary, out vec4 secondary) {
    primary.rgb = ambient_color.rgb;
    primary.a = 1;
    secondary.rgb = vec3(0);
    secondary.a = 1;

    for (int i=0;i<numlights;i++) {
        primary.rgb += light[i].ambient;

        float diffuselevel = max(quatrot(normquat, normalize(light[i].dir)).z, 0);
        primary.rgb += vec3(diffuselevel);
    }
}

bool run_alphatest(float a) {
    switch (alphafunc) {
        case 0: return false;
        case 1: return true;
        case 2: return a == alpharef;
        case 3: return a != alpharef;
        case 4: return a < alpharef;
        case 5: return a <= alpharef;
        case 6: return a > alpharef;
        case 7: return a >= alpharef;
        default: return true;
    }
}

void main() {
    tev_srcs[0] = color;
    calc_lighting(tev_srcs[1], tev_srcs[2]);
    tev_srcs[3] = texture(tex0, texcoord0);
    tev_srcs[4] = texture(tex1, texcoord1);
    tev_srcs[5] = texture(tex2, tex2coord ? texcoord1 : texcoord2);

    vec4 next_buffer = tev_buffer_color;
    for (int i = 0; i < 6; i++) {
        tev_srcs[14] = tev[i].color;

        vec4 res;
        res.rgb = tev_combine_rgb(i);
        if (tev[i].rgb.combiner == 7) {
            res.a = res.r;
        } else {
            res.a = tev_combine_alpha(i);
        }
        res.rgb *= tev[i].rgb.scale;
        res.a *= tev[i].a.scale;

        res = clamp(res, 0, 1);

        tev_srcs[13] = next_buffer;

        if ((tev_update_rgb & (1<<i)) != 0) {
            next_buffer.rgb = res.rgb;
        }
        if ((tev_update_alpha & (1<<i)) != 0) {
            next_buffer.a = res.a;
        }

        tev_srcs[15] = res;
    }

    fragclr = tev_srcs[15];

    if (alphatest && !run_alphatest(fragclr.a)) discard;

}

)";
