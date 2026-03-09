#version 330 core

in vec4 color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;
in float texcoordw;
in vec4 normquat;
in vec3 view;

out vec4 fragclr;

#define BIT(k, n) ((k&(1u<<n))!=0u)

#define TEVSRC_COLOR 0u
#define TEVSRC_LIGHT_PRIMARY 1u
#define TEVSRC_LIGHT_SECONDARY 2u
#define TEVSRC_TEX0 3u
#define TEVSRC_TEX1 4u
#define TEVSRC_TEX2 5u
#define TEVSRC_TEX3 6u
#define TEVSRC_BUFFER 13u
#define TEVSRC_CONSTANT 14u
#define TEVSRC_PREVIOUS 15u

#define LUT_D0 0u
#define LUT_D1 1u
#define LUT_SP 2u
#define LUT_FR 3u
#define LUT_RB 4u
#define LUT_RG 5u
#define LUT_RR 6u
#define LUT_DA 7u
#define LUT_SP_BASE 8u
#define LUT_DA_BASE 16u

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;

uniform sampler1DArray lightLuts;

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
    vec3 vec;
    vec3 spotdir;
    vec4 _pad;
    float attn_bias;
    float attn_scale;
};

layout(std140) uniform FragConfig {
    Tev tev[6];

    uint tev_update_rgb;
    uint tev_update_alpha;
    bool tex2coord;

    uint light_config[8];
    uint numlights;

    uint lconfig0;
    uint lconfig1;
    uint llutAbs;
    uint llutSel;
    uint llutScale;
    uint lightPerm;

    bool alphatest;
    uint alphafunc;
};

layout(std140) uniform FragUniforms {
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

vec4 tev_srcs[16];

const uint lightenvEnabledLuts[8] = uint[](
        0xc5u, 0xccu, 0xc3u, 0x8bu, 0xf7u, 0xfdu, 0xcfu, 0xffu
    );

float lutInputs[8];

float read_lut(uint lutNum) {
    uint lutCfgNum = (LUT_SP_BASE <= lutNum && lutNum <= LUT_SP_BASE + 7u) ? LUT_SP : lutNum;
    if (!BIT(lightenvEnabledLuts[lconfig0 >> 4 & 7u], lutCfgNum) || BIT(lconfig1, 16u + lutCfgNum)) {
        return 1.0f;
    }
    float lin = lutInputs[llutSel >> (4u * lutCfgNum) & 7u];
    if (!BIT(llutAbs, 4u * lutCfgNum + 1u)) lin = abs(lin);
    float res = texture(lightLuts, vec2((lin + 1) / 2, lutNum)).r;
    int scale = int(llutScale >> (4u * lutCfgNum) << 29) >> 29;
    return res * pow(2, scale);
}

void calc_lighting(out vec4 primary, out vec4 secondary) {
    primary = vec4(0);
    secondary = vec4(0);
    primary.a = 1;
    secondary.a = 1;

    primary.rgb = ambient_color.rgb;

    uint bumpMode = (lconfig0 >> 28) & 3u;
    uint bumpTex = (lconfig0 >> 22) & 3u;
    vec3 n = (bumpMode == 1u) ? tev_srcs[TEVSRC_TEX0 + bumpTex].xyz - 0.5f : vec3(0, 0, 1);
    vec3 t = (bumpMode == 2u) ? tev_srcs[TEVSRC_TEX0 + bumpTex].xyz - 0.5f : vec3(1, 0, 0);

    n = normalize(quatrot(normquat, n));
    t = normalize(quatrot(normquat, t));

    vec3 v = normalize(view);

    lutInputs[2] = dot(n, v);

    for (uint i = 0u; i < numlights; i++) {
        vec3 l;
        if (BIT(light_config[i], 0)) {
            l = normalize(light[i].vec);
        } else {
            l = normalize(view + light[i].vec);
        }
        vec3 h = normalize(l + v);
        vec3 h_proj = normalize(h - n * dot(n, h));
        vec3 p = normalize(light[i].spotdir);
        float dist = length(view + light[i].vec);

        float ndotl = dot(n, l);

        lutInputs[0] = dot(n, h);
        lutInputs[1] = dot(v, h);
        lutInputs[3] = ndotl;
        lutInputs[4] = dot(l, p);
        lutInputs[5] = dot(t, h_proj);

        if (BIT(light_config[i], 1)) {
            ndotl = abs(ndotl);
        } else {
            ndotl = max(ndotl, 0);
        }

        vec3 cp = light[i].ambient + ndotl * light[i].diffuse;

        float G = min(ndotl / dot(l + v, l + v), 1);
        float G0 = BIT(light_config[i], 2) ? G : 1;
        float G1 = BIT(light_config[i], 3) ? G : 1;

        vec3 R;
        // either only RR enabled, or all three
        if (BIT(lightenvEnabledLuts[lconfig0 >> 4 & 7u], LUT_RG)) {
            R = vec3(read_lut(LUT_RR), read_lut(LUT_RG), read_lut(LUT_RB));
        } else {
            R = vec3(read_lut(LUT_RR));
        }

        vec3 cs = read_lut(LUT_D0) * light[i].specular0 * G0 +
                read_lut(LUT_D1) * light[i].specular1 * R * G1;

        if (!BIT(lconfig1, 8u + i)) {
            float S = read_lut(LUT_SP_BASE + i);
            cp *= S;
            cs *= S;
        }
        if (BIT(lightenvEnabledLuts[lconfig0 >> 4 & 7u], LUT_DA) && !BIT(lconfig1, 24u + i)) {
            float A = texture(lightLuts,
                    vec2(light[i].attn_bias + light[i].attn_scale * dist,
                        LUT_DA_BASE + i)).r;
            cp *= A;
            cs *= A;
        }

        primary.rgb += cp;
        secondary.rgb += cs;
    }

    if (BIT(lconfig0, 2) || BIT(lconfig0, 3)) {
        float fr = read_lut(LUT_FR);
        if (BIT(lconfig0, 2)) primary.a = fr;
        if (BIT(lconfig0, 3)) secondary.a = fr;
    }

    primary = min(primary, 1);
    secondary = min(secondary, 1);
}

vec3 tev_operand_rgb(vec4 v, uint op) {
    switch (op) {
        case 0u:
        return v.rgb;
        case 1u:
        return 1 - v.rgb;
        case 2u:
        return vec3(v.a);
        case 3u:
        return vec3(1 - v.a);
        case 4u:
        return vec3(v.r);
        case 5u:
        return vec3(1 - v.r);
        case 8u:
        return vec3(v.g);
        case 9u:
        return vec3(1 - v.g);
        case 12u:
        return vec3(v.b);
        case 13u:
        return vec3(1 - v.b);
        default:
        return v.rgb;
    }
}

float tev_operand_alpha(vec4 v, uint op) {
    switch (op) {
        case 0u:
        return v.a;
        case 1u:
        return 1 - v.a;
        case 2u:
        return v.r;
        case 3u:
        return 1 - v.r;
        case 4u:
        return v.g;
        case 5u:
        return 1 - v.g;
        case 6u:
        return v.b;
        case 7u:
        return 1 - v.b;
        default:
        return v.a;
    }
}

vec3 tev_combine_rgb(uint i) {
    #define SRC(n) tev_operand_rgb(tev_srcs[tev[i].rgb.src##n], tev[i].rgb.op##n)
    switch (tev[i].rgb.combiner) {
        case 0u:
        return SRC(0);
        case 1u:
        return SRC(0) * SRC(1);
        case 2u:
        return SRC(0) + SRC(1);
        case 3u:
        return SRC(0) + SRC(1) - 0.5;
        case 4u:
        return mix(SRC(1), SRC(0), SRC(2));
        case 5u:
        return SRC(0) - SRC(1);
        case 6u:
        case 7u:
        return vec3(4 * dot(SRC(0) - 0.5, SRC(1) - 0.5));
        case 8u:
        return SRC(0) * SRC(1) + SRC(2);
        case 9u:
        return (SRC(0) + SRC(1)) * SRC(2);
        default:
        return SRC(0);
    }
    #undef SRC
}

float tev_combine_alpha(uint i) {
    #define SRC(n) tev_operand_alpha(tev_srcs[tev[i].a.src##n], tev[i].a.op##n)
    switch (tev[i].a.combiner) {
        case 0u:
        return SRC(0);
        case 1u:
        return SRC(0) * SRC(1);
        case 2u:
        return SRC(0) + SRC(1);
        case 3u:
        return SRC(0) + SRC(1) - 0.5;
        case 4u:
        return mix(SRC(1), SRC(0), SRC(2));
        case 5u:
        return SRC(0) - SRC(1);
        case 6u:
        case 7u:
        return 4 * (SRC(0) - 0.5) * (SRC(1) - 0.5);
        case 8u:
        return SRC(0) * SRC(1) + SRC(2);
        case 9u:
        return (SRC(0) + SRC(1)) * SRC(2);
        default:
        return SRC(0);
    }
    #undef SRC
}

bool run_alphatest(float a) {
    switch (alphafunc) {
        case 0u:
        return false;
        case 1u:
        return true;
        case 2u:
        return a == alpharef;
        case 3u:
        return a != alpharef;
        case 4u:
        return a < alpharef;
        case 5u:
        return a <= alpharef;
        case 6u:
        return a > alpharef;
        case 7u:
        return a >= alpharef;
        default:
        return true;
    }
}

void main() {
    tev_srcs[TEVSRC_COLOR] = color;
    tev_srcs[TEVSRC_TEX0] = texture(tex0, texcoord0);
    tev_srcs[TEVSRC_TEX1] = texture(tex1, texcoord1);
    tev_srcs[TEVSRC_TEX2] = texture(tex2, tex2coord ? texcoord1 : texcoord2);
    tev_srcs[TEVSRC_BUFFER] = tev_buffer_color;
    calc_lighting(tev_srcs[TEVSRC_LIGHT_PRIMARY], tev_srcs[TEVSRC_LIGHT_SECONDARY]);

    vec4 next_buf = tev_buffer_color;

    for (uint i = 0u; i < 6u; i++) {
        tev_srcs[TEVSRC_CONSTANT] = tev_color[i];

        vec4 res;
        res.rgb = tev_combine_rgb(i);
        if (tev[i].rgb.combiner == 7u) {
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
