#include "shadergen_fs.h"

#include <math.h>

#include "dynstring.h"

const char fs_header[] = R"(
#version 330 core

in vec4 color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;
in float texcoordw;
in vec3 normal;
in vec3 tangent;
in vec3 view;

out vec4 fragclr;

uniform sampler2D tex0;
uniform sampler2DShadow tex0shadow;

uniform sampler2D tex1;
uniform sampler2D tex2;

uniform sampler1D tex3;

uniform sampler1DArray lightLuts;
uniform sampler1D fogLut;
uniform sampler1D proctexMapLut;
uniform sampler1D proctexNoiseLut;

struct Light {
    vec3 specular0;
    vec3 specular1;
    vec3 diffuse;
    vec3 ambient;
    vec3 vec;
    vec4 spotdir;
    float attn_bias;
    float attn_scale;
};

struct PTNoiseParam {
    float ampl;
    float phase;
    float freq;
    float pad;
};

layout (std140) uniform FragUniforms {
    vec4 tev_color[6];
    vec4 tev_buffer_color;

    PTNoiseParam noiseU;
    PTNoiseParam noiseV;

    Light light[8];
    vec3 ambient_color;
    vec4 fog_color;

    float shadowMax;
    float shadowRamp;
    float shadowBias;
    float alpharef;
};

#define LUT_D0 0
#define LUT_D1 1
#define LUT_FR 3
#define LUT_RB 4
#define LUT_RG 5
#define LUT_RR 6
#define LUT_SP_BASE 8
#define LUT_DA_BASE 16

)";

const u8 lightenvEnabledLuts[8] = {
    0xc5, 0xcc, 0xc3, 0x8b, 0xf7, 0xfd, 0xcf, 0xff,
};

const char* lutinput_str(int lutSel) {
    switch (lutSel) {
        case 0:
            return "dot(n, h)";
        case 1:
            return "dot(v, h)";
        case 2:
            return "dot(n, v)";
        case 3:
            return "dot(n, l)";
        case 4:
            return "dot(l, p)";
        case 5:
            return "dot(t, h_proj)";
        default:
            return "0";
    }
}

void write_lut_read(DynString* s, FragConfig* fcfg, int lutNum, char* name) {
    ds_printf(s, "texture(lightLuts, vec2(");
    if (fcfg->llutAbs & BIT(4 * lutNum + 1)) {
        // to ensure proper clamping we need to multiply by the size of the
        // texture
        ds_printf(s, "fract(clamp(%s*128,-127.5,127.5)/256)",
                  lutinput_str(fcfg->llutSel >> 4 * lutNum & 7));
    } else {
        ds_printf(s, "abs(%s)", lutinput_str(fcfg->llutSel >> 4 * lutNum & 7));
    }
    ds_printf(s, ", %s)).r", name);
    int scaleExp = (sbit(3))(fcfg->llutScale >> 4 * lutNum);
    if (scaleExp != 0) {
        float scale = powf(2, scaleExp);
        ds_printf(s, "*%.2f", scale);
    }
}

void write_lighting(DynString* s, FragConfig* fcfg) {
    ds_printf(s, "vec3 n = normalize(normal);\n");
    ds_printf(s, "vec3 t = normalize(tangent);\n");

    if (fcfg->lconfig0.bumpMode != 0) {
        ds_printf(s, "vec3 bumpVec = 2 * tex%dc.xyz - 1;\n",
                  fcfg->lconfig0.bumpTex);
        if (!fcfg->lconfig0.noRecalcBumpVec) {
            // recalcuate z value, if this is set then z is calculated to make a
            // unit vector
            ds_printf(
                s,
                "bumpVec.z = sqrt(max(1 - dot(bumpVec.xy, bumpVec.xy), 0));\n");
        }
        // reconstruct the tbn matrix from normal and tangent and
        // use it to rotate the bump map vector
        // pica uses quaternions which are translated into normal/tangent
        // vectors in the vertex shader
        ds_printf(s, "mat3 tbn = mat3(t, cross(n,t), n);\n");
        if (fcfg->lconfig0.bumpMode == 1) {
            ds_printf(s, "n = normalize(tbn * bumpVec);\n");
        }
        if (fcfg->lconfig0.bumpMode == 2) {
            ds_printf(s, "t = normalize(tbn * bumpVec);\n");
        }
    }

    ds_printf(s, "vec3 v = normalize(view);\n");

    if (fcfg->lconfig0.shadow) {
        if (fcfg->lconfig0.shadowInv) {
            ds_printf(s, "vec4 H = 1 - tex%dc;\n", fcfg->lconfig0.shadowTex);
        } else {
            ds_printf(s, "vec4 H = tex%dc;\n", fcfg->lconfig0.shadowTex);
        }
    }

    ds_printf(s, "vec3 l, h, h_proj, p;\n");
    ds_printf(s, "float dist;\n");

    int envLuts = lightenvEnabledLuts[fcfg->lconfig0.lightenv];
    int disabledLuts = fcfg->lconfig1.luts;
    int enabledLuts = envLuts & ~disabledLuts;

    int i; // need this after the loop for fresnel
    for (int _i = 0; _i < fcfg->numlights; _i++) {
        i = fcfg->lightPerm >> 4 * _i & 7;

        if (fcfg->light[i].config.directional) {
            ds_printf(s, "l = light[%d].vec;\n", i);
        } else {
            ds_printf(s, "l = view + light[%d].vec;\n", i);
        }
        ds_printf(s, "dist = length(l);\n");
        ds_printf(s, "l /= dist;\n");
        ds_printf(s, "h = normalize(l + v);\n");
        ds_printf(s, "h_proj = normalize(h - n * dot(n, h));\n");
        ds_printf(s, "p = normalize(light[%d].spotdir.xyz);\n", i);

        const char* ndotlstr = fcfg->light[i].config.twosided
                                   ? "abs(dot(n, l))"
                                   : "max(dot(n, l), 0)";

        ds_printf(s, "vec3 cp_%d = %s * light[%d].diffuse;\n", i, ndotlstr, i,
                  i);

        if (fcfg->light[i].config.use_g0 || fcfg->light[i].config.use_g1) {
            ds_printf(s, "float G_%d = %s/dot(l+v,l+v);\n", i, ndotlstr);
        }

        bool use_R = false;
        if (envLuts & BIT(LLUT_RR) &&
            (enabledLuts & (BIT(LLUT_RR) | BIT(LLUT_RG) | BIT(LLUT_RB)))) {
            use_R = true;
            ds_printf(s, "vec3 R_%d = vec3(", i);
            if (envLuts & BIT(LLUT_RG)) {
                if (enabledLuts & BIT(LLUT_RR)) {
                    write_lut_read(s, fcfg, LLUT_RR, "LUT_RR");
                } else {
                    ds_printf(s, "1");
                }
                ds_printf(s, ", ");
                if (enabledLuts & BIT(LLUT_RG)) {
                    write_lut_read(s, fcfg, LLUT_RG, "LUT_RG");
                } else {
                    ds_printf(s, "1");
                }
                ds_printf(s, ", ");
                if (enabledLuts & BIT(LLUT_RB)) {
                    write_lut_read(s, fcfg, LLUT_RB, "LUT_RB");
                } else {
                    ds_printf(s, "1");
                }
            } else {
                if (enabledLuts & BIT(LLUT_RR)) {
                    write_lut_read(s, fcfg, LLUT_RR, "LUT_RR");
                } else {
                    ds_printf(s, "1");
                }
            }
            ds_printf(s, ");\n");
        }

        ds_printf(s, "vec3 cs_%d = ", i);
        if (enabledLuts & BIT(LLUT_D0)) {
            write_lut_read(s, fcfg, LLUT_D0, "LUT_D0");
            ds_printf(s, " * ");
        }
        if (fcfg->light[i].config.use_g0) {
            ds_printf(s, "G_%d * ", i);
        }
        ds_printf(s, "light[%d].specular0 +\n", i);
        if (enabledLuts & BIT(LLUT_D1)) {
            write_lut_read(s, fcfg, LLUT_D1, "LUT_D1");
            ds_printf(s, " * ");
        }
        if (use_R) {
            ds_printf(s, "R_%d * ", i);
        }
        if (fcfg->light[i].config.use_g1) {
            ds_printf(s, "G_%d * ", i);
        }
        ds_printf(s, "light[%d].specular1;\n", i);

        if (!(fcfg->lconfig1.shadow & BIT(i))) {
            if (fcfg->lconfig0.shadowPrimary) {
                ds_printf(s, "cp_%d *= H.rgb;\n", i);
            }
            if (fcfg->lconfig0.shadowSecondary) {
                ds_printf(s, "cs_%d *= H.rgb;\n", i);
            }
        }

        ds_printf(s, "cp_%d += light[%d].ambient;\n", i, i);

        if (fcfg->lconfig0.clampHighlights) {
            ds_printf(s, "float f_%d = float(dot(n,l)>=0);\n", i);
            ds_printf(s, "cp_%d *= f_%d;\n", i, i);
            ds_printf(s, "cs_%d *= f_%d;\n", i, i);
        }

        if (envLuts & BIT(LLUT_SP) && !(fcfg->lconfig1.spotlight & BIT(i))) {
            ds_printf(s, "float S_%d = ", i);
            char buf[64];
            snprintf(buf, sizeof buf, "LUT_SP_BASE + %d", i);
            write_lut_read(s, fcfg, LLUT_SP, buf);
            ds_printf(s, ";\n");
            ds_printf(s, "cp_%d *= S_%d;\n", i, i);
            ds_printf(s, "cs_%d *= S_%d;\n", i, i);
        }
        if (envLuts & BIT(LLUT_DA) && !(fcfg->lconfig1.distattn & BIT(i))) {
            ds_printf(s,
                      "float A_%d = texture(lightLuts, vec2(\n"
                      "light[%d].attn_bias + light[%d].attn_scale * dist,\n"
                      "LUT_DA_BASE + %d)).r;\n",
                      i, i, i, i);
            ds_printf(s, "cp_%d *= A_%d;\n", i, i);
            ds_printf(s, "cs_%d *= A_%d;\n", i, i);
        }

        ds_printf(s, "lprimary.rgb += cp_%d;\n", i);
        ds_printf(s, "lsecondary.rgb += cs_%d;\n", i);
    }

    if (enabledLuts & BIT(LLUT_FR) &&
        (fcfg->lconfig0.frPrimary || fcfg->lconfig0.frSecondary)) {
        ds_printf(s, "float fr = ");
        write_lut_read(s, fcfg, LLUT_FR, "LUT_FR");
        ds_printf(s, ";\n");
        if (fcfg->lconfig0.frPrimary) {
            ds_printf(s, "lprimary.a = fr;\n");
        } else {
            ds_printf(s, "lprimary.a = 1;\n");
        }
        if (fcfg->lconfig0.frSecondary) {
            ds_printf(s, "lsecondary.a = fr;\n");
        } else {
            ds_printf(s, "lsecondary.a = 1;\n");
        }
    } else {
        ds_printf(s, "lprimary.a = 1;\n");
        ds_printf(s, "lsecondary.a = 1;\n");
    }

    if (fcfg->lconfig0.shadowAlpha) {
        ds_printf(s, "lprimary.a *= H.a;\n");
        ds_printf(s, "lsecondary.a *= H.a;\n");
    }

    ds_printf(s, "lprimary.rgb += ambient_color;\n");

    ds_printf(s, "lprimary = min(lprimary, 1);\n");
    ds_printf(s, "lsecondary = min(lsecondary, 1);\n");
}

void write_proctex_clamp(DynString* s, const char* v, u32 mode) {
    switch (mode) {
        case 0:
            ds_printf(s, "%s = mix(max(%s,0), 0, %s>=1);\n", v, v, v);
            break;
        case 1:
            ds_printf(s, "%s = clamp(%s,0,1);\n", v, v);
            break;
        case 2:
            ds_printf(s, "%s = fract(%s);\n", v, v);
            break;
        case 3:
            ds_printf(s, "%s = mix(fract(%s),fract(-%s),(int(%s) & 1) == 1);\n",
                      v, v, v, v);
            break;
        case 4:
            ds_printf(s, "%s = float(%s > 0.5)", v, v);
            break;
    }
}

const char* proctex_combiner_str(u32 combiner) {
    switch (combiner) {
        case 0:
            return "texcoord3.x";
        case 1:
            return "texcoord3.x * texcoord3.x";
        case 2:
            return "texcoord3.y";
        case 3:
            return "texcoord3.y * texcoord3.y";
        case 4:
            return "0.5 * (texcoord3.x + texcoord3.y)";
        case 5:
            return "0.5 * dot(texcoord3, texcoord3)";
        case 6:
            return "min(length(texcoord3), 1)";
        case 7:
            return "min(texcoord3.x, texcoord3.y)";
        case 8:
            return "max(texcoord3.x, texcoord3.y)";
        case 9:
            return "0.5 * (0.5 * (texcoord3.x + texcoord3.y) + "
                   "min(length(texcoord3), 1))";
        default:
            return "0";
    }
}

void write_proctex(DynString* s, FragConfig* fcfg) {
    ds_printf(s, "vec2 texcoord3 = texcoord%d;\n", fcfg->texconfig.tex3coord);

    if (fcfg->proctex.noise) {
        ds_printf(s, "texcoord3.x += noiseU.ampl * "
                     "texture(proctexNoiseLut, (texcoord3.x + noiseU.phase) / "
                     "noiseU.freq).r;\n");
        ds_printf(s, "texcoord3.y += noiseV.ampl * "
                     "texture(proctexNoiseLut, (texcoord3.y + noiseV.phase) / "
                     "noiseV.freq).r;\n");
    }

    write_proctex_clamp(s, "texcoord3.x", fcfg->proctex.clampU);
    write_proctex_clamp(s, "texcoord3.y", fcfg->proctex.clampV);

    ds_printf(s, "vec4 tex3c = texture(tex3, texture(proctexMapLut, %s).r);\n",
              proctex_combiner_str(fcfg->proctex.rgbCombiner));
    if (fcfg->proctex.separateAlpha) {
        ds_printf(s,
                  "tex3c.a = texture(tex3, texture(proctexMapLut, %s).g).a;\n",
                  proctex_combiner_str(fcfg->proctex.alphaCombiner));
    }
}

const char* tevsrc_str(int i, u32 tevsrc) {
    switch (tevsrc) {
        case TEVSRC_COLOR:
            return "color";
        case TEVSRC_LIGHT_PRIMARY:
            return "lprimary";
        case TEVSRC_LIGHT_SECONDARY:
            return "lsecondary";
        case TEVSRC_TEX0:
            return "tex0c";
        case TEVSRC_TEX1:
            return "tex1c";
        case TEVSRC_TEX2:
            return "tex2c";
        case TEVSRC_TEX3:
            return "tex3c";
        case TEVSRC_BUFFER:
            return "buf";
        case TEVSRC_CONSTANT: {
            // this is cursed
            switch (i) {
                case 0:
                    return "tev_color[0]";
                case 1:
                    return "tev_color[1]";
                case 2:
                    return "tev_color[2]";
                case 3:
                    return "tev_color[3]";
                case 4:
                    return "tev_color[4]";
                case 5:
                    return "tev_color[5]";
                default:
                    unreachable();
            }
        }
        case TEVSRC_PREVIOUS:
            return "cur";
        default:
            return "vec4(0)";
    }
}

void write_operand_rgb(DynString* s, const char* srcstr, u32 op) {
    switch (op) {
        case 0:
            ds_printf(s, "%s.rgb", srcstr);
            break;
        case 1:
            ds_printf(s, "(1 - %s.rgb)", srcstr);
            break;
        case 2:
            ds_printf(s, "%s.aaa", srcstr);
            break;
        case 3:
            ds_printf(s, "(1 - %s.aaa)", srcstr);
            break;
        case 4:
            ds_printf(s, "%s.rrr", srcstr);
            break;
        case 5:
            ds_printf(s, "(1 - %s.rrr)", srcstr);
            break;
        case 8:
            ds_printf(s, "%s.ggg", srcstr);
            break;
        case 9:
            ds_printf(s, "(1 - %s.ggg)", srcstr);
            break;
        case 12:
            ds_printf(s, "%s.bbb", srcstr);
            break;
        case 13:
            ds_printf(s, "(1 - %s.bbb)", srcstr);
            break;
        default:
            ds_printf(s, "%s.rgb", srcstr);
    }
}

void write_operand_a(DynString* s, const char* srcstr, u32 op) {
    switch (op) {
        case 0:
            ds_printf(s, "%s.a", srcstr);
            break;
        case 1:
            ds_printf(s, "(1 - %s.a)", srcstr);
            break;
        case 2:
            ds_printf(s, "%s.r", srcstr);
            break;
        case 3:
            ds_printf(s, "(1 - %s.r)", srcstr);
            break;
        case 4:
            ds_printf(s, "%s.g", srcstr);
            break;
        case 5:
            ds_printf(s, "(1 - %s.g)", srcstr);
            break;
        case 6:
            ds_printf(s, "%s.b", srcstr);
            break;
        case 7:
            ds_printf(s, "(1 - %s.b)", srcstr);
            break;
        default:
            ds_printf(s, "%s.a", srcstr);
    }
}

void write_combiner_rgb(DynString* s, FragConfig* fcfg, int i) {
#define SRC(n)                                                                 \
    write_operand_rgb(s, tevsrc_str(i, fcfg->tev[i].rgb.src##n),               \
                      fcfg->tev[i].rgb.op##n)
    switch (fcfg->tev[i].rgb.combiner) {
        case 0:
            SRC(0);
            break;
        case 1:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            break;
        case 2:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, ", 1)");
            break;
        case 3:
            ds_printf(s, "clamp(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, " - 0.5, 0, 1)");
            break;
        case 4:
            ds_printf(s, "mix(");
            SRC(1);
            ds_printf(s, ", ");
            SRC(0);
            ds_printf(s, ", ");
            SRC(2);
            ds_printf(s, ")");
            break;
        case 5:
            ds_printf(s, "max(");
            SRC(0);
            ds_printf(s, " - ");
            SRC(1);
            ds_printf(s, ", 0)");
            break;
        case 6:
        case 7:
            ds_printf(s, "max(vec3(4 * dot(");
            SRC(0);
            ds_printf(s, " - 0.5, ");
            SRC(1);
            ds_printf(s, " - 0.5)), 0)");
            break;
        case 8:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, " + ");
            SRC(2);
            ds_printf(s, ", 1)");
            break;
        case 9:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, ", 1) * ");
            SRC(2);
            break;
        default:
            SRC(0);
            break;
    }
#undef SRC
}

void write_combiner_a(DynString* s, FragConfig* fcfg, int i) {
#define SRC(n)                                                                 \
    write_operand_a(s, tevsrc_str(i, fcfg->tev[i].a.src##n),                   \
                    fcfg->tev[i].a.op##n)
    switch (fcfg->tev[i].a.combiner) {
        case 0:
            SRC(0);
            break;
        case 1:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            break;
        case 2:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, ", 1)");
            break;
        case 3:
            ds_printf(s, "clamp(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, " - 0.5, 0, 1)");
            break;
        case 4:
            ds_printf(s, "mix(");
            SRC(1);
            ds_printf(s, ", ");
            SRC(0);
            ds_printf(s, ", ");
            SRC(2);
            ds_printf(s, ")");
            break;
        case 5:
            ds_printf(s, "max(");
            SRC(0);
            ds_printf(s, " - ");
            SRC(1);
            ds_printf(s, ", 0)");
            break;
        case 6:
        case 7:
            ds_printf(s, "max(4 * (");
            SRC(0);
            ds_printf(s, " - 0.5) * (");
            SRC(1);
            ds_printf(s, " - 0.5), 0)");
            break;
        case 8:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, " + ");
            SRC(2);
            ds_printf(s, ", 1)");
            break;
        case 9:
            ds_printf(s, "min(");
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, ", 1) * ");
            SRC(2);
            break;
        default:
            SRC(0);
            break;
    }
#undef SRC
}

const char* alphatest_str(int alphafunc) {
    switch (alphafunc) {
        case 0:
            return "false";
        case 1:
            return "true";
        case 2:
            return "(fragclr.a == alpharef)";
        case 3:
            return "(fragclr.a != alpharef)";
        case 4:
            return "(fragclr.a < alpharef)";
        case 5:
            return "(fragclr.a <= alpharef)";
        case 6:
            return "(fragclr.a > alpharef)";
        case 7:
            return "(fragclr.a >= alpharef)";
        default:
            return "true";
    }
}

char* shader_gen_fs(FragConfig* fcfg) {
    DynString s;
    ds_init(&s, 8192);

    ds_printf(&s, fs_header);

    ds_printf(&s, "void main() {\n");

    if (fcfg->texconfig.tex0enable) {
        switch (fcfg->tex0type) {
            case 0: // normal
                ds_printf(&s, "vec4 tex0c = texture(tex0, texcoord0);\n");
                break;
            case 2: // shadow map
                // shadow map sampling
                // texcoord0.uvw is the fragment position in light space
                // we read out the depth from the shadow map and compare
                // with texcoord0.w which is fragment depth in light space
                // to determine if it is in the shadow
                // previously we did this by hand but now are using
                // sampler2DShadow which does all this for us
                ds_printf(&s,
                          "vec4 tex0c = vec4("
                          "1 - texture(tex0shadow, vec3(texcoord0%s, "
                          "texcoordw-shadowBias)));\n",
                          fcfg->shadowPerspective ? "/texcoordw" : "");
                break;
            case 1: // cube
            case 4: // shadow cube
                lwarnonce("cube map texture");
                ds_printf(&s, "vec4 tex0c = vec4(1);\n");
                break;
            case 3: // projection
                ds_printf(&s, "vec4 tex0c = textureProj(tex0, vec3(texcoord0, "
                              "texcoordw));\n");
                break;
            default:
                lwarnonce("unknown texture type %d", fcfg->tex0type);
                ds_printf(&s, "vec4 tex0c = vec4(0);\n");
        }
    } else ds_printf(&s, "vec4 tex0c = vec4(0);\n");
    if (fcfg->texconfig.tex1enable) {
        ds_printf(&s, "vec4 tex1c = texture(tex1, texcoord1);\n");
    } else ds_printf(&s, "vec4 tex1c = vec4(0);\n");
    if (fcfg->texconfig.tex2enable) {
        ds_printf(&s, "vec4 tex2c = texture(tex2, texcoord%d);\n",
                  fcfg->texconfig.tex2coord ? 1 : 2);
    } else ds_printf(&s, "vec4 tex2c = vec4(0);\n");
    if (fcfg->texconfig.tex3enable) {
        write_proctex(&s, fcfg);
    } else ds_printf(&s, "vec4 tex3c = vec4(0);\n");

    ds_printf(&s, "vec4 lprimary = vec4(0);\n");
    ds_printf(&s, "vec4 lsecondary = vec4(0);\n");
    if (!fcfg->lightDisable) {
        write_lighting(&s, fcfg);
    }

    ds_printf(&s, R"(
vec4 cur = vec4(0);
vec4 buf = tev_buffer_color;
vec4 tmp;
)");

    for (int i = 0; i < 6; i++) {
        // check for do nothing stage
        bool skiprgb = fcfg->tev[i].rgb.combiner == 0 &&
                       fcfg->tev[i].rgb.op0 == 0 &&
                       fcfg->tev[i].rgb.src0 == TEVSRC_PREVIOUS;
        bool skipa = fcfg->tev[i].a.combiner == 0 && fcfg->tev[i].a.op0 == 0 &&
                     fcfg->tev[i].a.src0 == TEVSRC_PREVIOUS;
        if (!(skiprgb && skipa)) {
            ds_printf(&s, "tmp.rgb = ");
            write_combiner_rgb(&s, fcfg, i);
            ds_printf(&s, ";\n");
            if (fcfg->tev[i].rgb.combiner == 7) {
                ds_printf(&s, "tmp.a = tmp.r;\n");
            } else {
                ds_printf(&s, "tmp.a = ");
                write_combiner_a(&s, fcfg, i);
                ds_printf(&s, ";\n");
            }
        }

        if (i > 0) {
            // buffer update should happen in parallel
            // with the tev combiner
            // input from previous stage and not visible until next stage
            if (fcfg->tev_buffer.update_rgb & BIT(i - 1)) {
                ds_printf(&s, "buf.rgb = cur.rgb;\n");
            }
            if (fcfg->tev_buffer.update_alpha & BIT(i - 1)) {
                ds_printf(&s, "buf.a = cur.a;\n");
            }
        }

        if (!(skiprgb && skipa)) {
            ds_printf(&s, "cur = tmp;\n");
        }

        if (fcfg->tev[i].rgb.scale != 1.0f) {
            ds_printf(&s, "cur.rgb = min(cur.rgb * %.0f, 1);\n",
                      fcfg->tev[i].rgb.scale);
        }
        if (fcfg->tev[i].a.scale != 1.0f) {
            ds_printf(&s, "cur.a = min(cur.a * %.0f, 1);\n",
                      fcfg->tev[i].a.scale);
        }
    }

    ds_printf(&s, "fragclr = clamp(cur, 0, 1);\n");

    if (fcfg->alphatest) {
        ds_printf(&s, "if (!%s) discard;\n", alphatest_str(fcfg->alphafunc));
    }

    if (fcfg->tev_buffer.fogmode == 5) {
        ds_printf(&s, "float fog = texture(fogLut, ");
        if (fcfg->tev_buffer.zflip) {
            ds_printf(&s, "1 - ");
        }
        // gl will take coordinate n.5 as the value at n in the texture
        // and coordinate n is actually interpolated between n-1,n
        // but we want to shift over by 0.5
        // coordinate 0/128 -> 0.5, ... 127/128 -> 127.5, 128/128 -> 128.5
        // for proper fog we recorrect the coordinate (it is quite sensitive to
        // this)
        ds_printf(&s, "(gl_FragCoord.z*128.f/129+0.5f/129)).r;\n");
        ds_printf(&s, "fragclr.rgb = mix(fog_color.rgb, fragclr.rgb, fog);\n");
    }

    ds_printf(&s, "}\n");

    return s.str;
}
