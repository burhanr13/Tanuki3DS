#include "shadergen_fs.h"

#include <math.h>

const char fs_header[] = R"(
#version 330 core

in vec4 color;
in vec2 texcoord0;
in vec2 texcoord1;
in vec2 texcoord2;
in float texcoordw;
in vec4 normquat;
in vec3 view;

out vec4 fragclr;

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;

uniform sampler1DArray lightLuts;

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

layout (std140) uniform FragUniforms {
    vec4 tev_color[6];
    vec4 tev_buffer_color;

    Light light[8];
    vec4 ambient_color;

    float shadowBias;
    float alpharef;
};

vec3 quatrot(vec4 q, vec3 v) {
    return 2 * (q.w * cross(q.xyz, v) + q.xyz * dot(q.xyz, v)) +
           (q.w * q.w - dot(q.xyz, q.xyz)) * v;
}

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

void write_lut_read(DynString* s, FragConfig* fcfg, int lutNum, char* name,
                    bool twosided) {
    ds_printf(s, "texture(lightLuts, vec2(");
    if (fcfg->llutAbs & BIT(4 * lutNum + 1)) {
        // to ensure proper clamping we need to multiply by the size of the
        // texture
        ds_printf(s, "fract(clamp(%s*128,-127.5,127.5)/256)",
                  lutinput_str(fcfg->llutSel >> 4 * lutNum & 7));
    } else {
        if (twosided) {
            ds_printf(s, "abs(%s)",
                      lutinput_str(fcfg->llutSel >> 4 * lutNum & 7));
        } else {
            ds_printf(s, "max(%s, 0)",
                      lutinput_str(fcfg->llutSel >> 4 * lutNum & 7));
        }
    }
    ds_printf(s, ", %s)).r", name);
    int scaleExp = (sbit(3))(fcfg->llutScale >> 4 * lutNum);
    if (scaleExp != 0) {
        float scale = powf(2, scaleExp);
        ds_printf(s, "*%.2f", scale);
    }
}

void write_lighting(DynString* s, FragConfig* fcfg) {
    ds_printf(s, "vec4 lprimary = vec4(0);\n");
    ds_printf(s, "vec4 lsecondary = vec4(0);\n");

    if (fcfg->lconfig0.bumpMode != 0) {
        ds_printf(s, "vec3 bumpVec = 2 * tex%dc.xyz - 1;\n",
                  fcfg->lconfig0.bumpTex);
        if (!(fcfg->lconfig0.noRecalcBumpVec)) {
            // recalcuate z value, if this is set then z is calculated to make a
            // unit vector
            ds_printf(
                s,
                "bumpVec.z = sqrt(max(1 - dot(bumpVec.xy, bumpVec.xy), 0));\n");
        }
    }
    if (fcfg->lconfig0.bumpMode == 1) {
        ds_printf(s, "vec3 n = normalize(quatrot(normquat, bumpVec));\n");
    } else {
        ds_printf(s, "vec3 n = normalize(quatrot(normquat, vec3(0, 0, 1)));\n");
    }
    if (fcfg->lconfig0.bumpMode == 2) {
        ds_printf(s, "vec3 t = normalize(quatrot(normquat, bumpVec));\n");
    } else {
        ds_printf(s, "vec3 t = normalize(quatrot(normquat, vec3(1, 0, 0)));\n");
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
        ds_printf(s, "p = normalize(light[%d].spotdir);\n", i);

        ds_printf(s, "vec3 cp_%d = light[%d].ambient + ", i, i);
        if (fcfg->light[i].config.twosided) {
            ds_printf(s, "abs(dot(n, l)) * ");
        } else {
            ds_printf(s, "max(dot(n, l), 0) * ");
        }
        ds_printf(s, "light[%d].diffuse;\n", i);

        if (fcfg->light[i].config.use_g0 || fcfg->light[i].config.use_g1) {
            ds_printf(s, "float G_%d = clamp(dot(n,l)/dot(l+v,l+v),0,1);\n", i);
        }

        bool use_R = false;
        if (envLuts & BIT(LLUT_RR) &&
            (enabledLuts & (BIT(LLUT_RR) | BIT(LLUT_RG) | BIT(LLUT_RB)))) {
            use_R = true;
            ds_printf(s, "vec3 R_%d = vec3(", i);
            if (envLuts & BIT(LLUT_RG)) {
                if (enabledLuts & BIT(LLUT_RR)) {
                    write_lut_read(s, fcfg, LLUT_RR, "LUT_RR",
                                   fcfg->light[i].config.twosided);
                } else {
                    ds_printf(s, "1");
                }
                ds_printf(s, ", ");
                if (enabledLuts & BIT(LLUT_RG)) {
                    write_lut_read(s, fcfg, LLUT_RG, "LUT_RG",
                                   fcfg->light[i].config.twosided);
                } else {
                    ds_printf(s, "1");
                }
                ds_printf(s, ", ");
                if (enabledLuts & BIT(LLUT_RB)) {
                    write_lut_read(s, fcfg, LLUT_RB, "LUT_RB",
                                   fcfg->light[i].config.twosided);
                } else {
                    ds_printf(s, "1");
                }
            } else {
                if (enabledLuts & BIT(LLUT_RR)) {
                    write_lut_read(s, fcfg, LLUT_RR, "LUT_RR",
                                   fcfg->light[i].config.twosided);
                } else {
                    ds_printf(s, "1");
                }
            }
            ds_printf(s, ");\n");
        }

        ds_printf(s, "vec3 cs_%d = ", i);
        if (enabledLuts & BIT(LLUT_D0)) {
            write_lut_read(s, fcfg, LLUT_D0, "LUT_D0",
                           fcfg->light[i].config.twosided);
            ds_printf(s, " * ");
        }
        if (fcfg->light[i].config.use_g0) {
            ds_printf(s, "G_%d * ", i);
        }
        ds_printf(s, "light[%d].specular0 +\n", i);
        if (enabledLuts & BIT(LLUT_D1)) {
            write_lut_read(s, fcfg, LLUT_D1, "LUT_D1",
                           fcfg->light[i].config.twosided);
            ds_printf(s, " * ");
        }
        if (use_R) {
            ds_printf(s, "R_%d * ", i);
        }
        if (fcfg->light[i].config.use_g1) {
            ds_printf(s, "G_%d * ", i);
        }
        ds_printf(s, "light[%d].specular1;\n", i);

        if (envLuts & BIT(LLUT_SP) && !(fcfg->lconfig1.spotlight & BIT(i))) {
            ds_printf(s, "float S_%d = ", i);
            char buf[64];
            snprintf(buf, sizeof buf, "LUT_SP_BASE + %d", i);
            write_lut_read(s, fcfg, LLUT_SP, buf,
                           fcfg->light[i].config.twosided);
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

        if (!(fcfg->lconfig1.shadow & BIT(i))) {
            if (fcfg->lconfig0.shadowPrimary) {
                ds_printf(s, "cp_%d *= H.rgb;\n", i);
            }
            if (fcfg->lconfig0.shadowSecondary) {
                ds_printf(s, "cs_%d *= H.rgb;\n", i);
            }
        }

        ds_printf(s, "lprimary.rgb += cp_%d;\n", i);
        ds_printf(s, "lsecondary.rgb += cs_%d;\n", i);
    }

    if (enabledLuts & BIT(LLUT_FR) &&
        (fcfg->lconfig0.frPrimary || fcfg->lconfig0.frSecondary)) {
        ds_printf(s, "float fr = ");
        write_lut_read(s, fcfg, LLUT_FR, "LUT_FR",
                       fcfg->light[i].config.twosided);
        ds_printf(s, ";\n");
    }
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
    if (fcfg->lconfig0.shadowAlpha) {
        ds_printf(s, "lprimary.a *= H;\n");
        ds_printf(s, "lsecondary.a *= H;\n");
    }

    ds_printf(s, "lprimary.rgb += ambient_color.rgb;\n");

    ds_printf(s, "lprimary = min(lprimary, 1);\n");
    ds_printf(s, "lsecondary = min(lsecondary, 1);\n");
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
            ds_printf(s, "1 - %s.aaa", srcstr);
            break;
        case 4:
            ds_printf(s, "%s.rrr", srcstr);
            break;
        case 5:
            ds_printf(s, "1 - %s.rrr", srcstr);
            break;
        case 8:
            ds_printf(s, "%s.ggg", srcstr);
            break;
        case 9:
            ds_printf(s, "1 - %s.ggg", srcstr);
            break;
        case 12:
            ds_printf(s, "%s.bbb", srcstr);
            break;
        case 13:
            ds_printf(s, "1 - %s.bbb", srcstr);
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

    if (fcfg->tex0shadow) {
        // shadow map sampling
        // texcoord0.uvw is the fragment position in light space
        // we read out the depth from the shadow map and compare
        // with texcoord0.w which is fragment depth in light space
        // to determine if it is in the shadow
        ds_printf(&s, "vec4 tex0c = vec4(texture(tex0, texcoord0");
        if (fcfg->shadowPerspective) {
            ds_printf(&s, "/texcoordw");
        }
        ds_printf(&s, ").r+shadowBias > min(texcoordw,1));\n");
    } else {
        ds_printf(&s, "vec4 tex0c = texture(tex0, texcoord0);\n");
    }
    ds_printf(&s, "vec4 tex1c = texture(tex1, texcoord1);\n");
    ds_printf(&s, "vec4 tex2c = texture(tex2, texcoord%d);\n",
              fcfg->tex2coord ? 1 : 2);
    // todo: proctex
    ds_printf(&s, "vec4 tex3c = vec4(1);\n");

    if (fcfg->lightDisable) {
        ds_printf(&s, "vec4 lprimary = vec4(1);\n");
        ds_printf(&s, "vec4 lsecondary = vec4(1);\n");
    } else {
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

    if (fcfg->fragOp == 3) {
        // for shadow map, r is the depth in light space
        ds_printf(&s, "fragclr.r = gl_FragCoord.z;\n");
        // g is the intensity?
        ds_printf(&s, "fragclr.g = 1;\n");
        // b and a are unused?
        ds_printf(&s, "fragclr.b = 0;\n");
        ds_printf(&s, "fragclr.a = 1;\n");
    }

    ds_printf(&s, "}\n");

    return s.str;
}
