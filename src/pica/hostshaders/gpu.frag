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

#define BIT(k, n) ((k&(1u<<n))!=0)

#define TEVSRC_COLOR 0
#define TEVSRC_LIGHT_PRIMARY 1
#define TEVSRC_LIGHT_SECONDARY 2
#define TEVSRC_TEX0 3
#define TEVSRC_TEX1 4
#define TEVSRC_TEX2 5
#define TEVSRC_TEX3 6
#define TEVSRC_BUFFER 13
#define TEVSRC_CONSTANT 14
#define TEVSRC_PREVIOUS 15

#define L_DIRECTIONAL 0

struct TevControl {
    uint src0;
    uint op0;
    uint src1;
    uint op1;
    uint src2;
    uint op2;
    uint combiner;
    float scale;
};

struct Tev {
    TevControl rgb;
    TevControl a;
};

struct Light {
    vec3 specular0;
    vec3 specular1;
    vec3 diffuse;
    vec3 ambient;
    vec4 vec;
};

layout (std140) uniform UberUniforms {
    Tev tev[6];

    uint tev_update_rgb;
    uint tev_update_alpha;
    bool tex2coord;

    uint light_config[8];
    uint numlights;

    bool alphatest;
    uint alphafunc;
};

layout (std140) uniform FragUniforms {
    vec4 tev_color[6];
    vec4 tev_buffer_color;

    Light light[8];
    vec4 ambient_color;

    float alpharef;
};

// q * v * q^-1
vec3 quatrot(vec4 q, vec3 v) {
    return 2 * (q.w * cross(q.xyz, v) + q.xyz * dot(q.xyz, v)) +
           (q.w * q.w - dot(q.xyz, q.xyz)) * v;
}

void calc_lighting(out vec4 primary, out vec4 secondary) {
    primary = vec4(0);
    secondary = vec4(0);

    primary.rgb = ambient_color.rgb;
    primary.a = 1;

    secondary.a = 0.5;

    vec4 nq = normalize(normquat);
    vec3 v = normalize(quatrot(nq, view));

    for (int i=0;i<numlights;i++) {
        primary.rgb += light[i].ambient;

        vec3 l;
        if (BIT(light_config[i], L_DIRECTIONAL)) {
            l = normalize(quatrot(nq, light[i].vec.xyz));
        } else {
            l = normalize(quatrot(nq, view + light[i].vec.xyz));
        }
        vec3 h = normalize(l + v);

        // right now we just use phong shading
        // TODO: implement this properly

        float diffuselevel = max(l.z, 0);
        primary.rgb += diffuselevel * light[i].diffuse;

        primary.rgb = min(primary.rgb, 1);

        float speclevel = pow(max(h.z, 0), 3);
        secondary.rgb += speclevel * light[i].specular0;

        secondary.rgb = min(secondary.rgb, 1);
    }
}

vec4 tev_srcs[16];

vec3 tev_operand_rgb(vec4 v, uint op) {
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

float tev_operand_alpha(vec4 v, uint op) {
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

vec3 tev_combine_rgb(uint i) {
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

float tev_combine_alpha(uint i) {
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
    tev_srcs[TEVSRC_COLOR] = color;
    calc_lighting(tev_srcs[TEVSRC_LIGHT_PRIMARY], tev_srcs[TEVSRC_LIGHT_SECONDARY]);
    tev_srcs[TEVSRC_TEX0] = texture(tex0, texcoord0);
    tev_srcs[TEVSRC_TEX1] = texture(tex1, texcoord1);
    tev_srcs[TEVSRC_TEX2] = texture(tex2, tex2coord ? texcoord1 : texcoord2);
    tev_srcs[TEVSRC_BUFFER] = tev_buffer_color;

    vec4 next_buf = tev_buffer_color;

    for (uint i = 0; i < 6; i++) {
        tev_srcs[TEVSRC_CONSTANT] = tev_color[i];

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

        tev_srcs[TEVSRC_BUFFER] = next_buf;

        if (BIT(tev_update_rgb, i)) {
            next_buf.rgb = res.rgb;
        }
        if (BIT(tev_update_alpha, i)) {
            next_buf.a = res.a;
        }

        tev_srcs[TEVSRC_PREVIOUS] = res;
    }

    fragclr = tev_srcs[TEVSRC_PREVIOUS];

    if (alphatest && !run_alphatest(fragclr.a)) discard;

}

