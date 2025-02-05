#include "shadergen.h"

#include <xxh3.h>

#include "gpu.h"

int shader_gen_get(GPU* gpu, UberUniforms* ubuf) {
    u64 hash = XXH3_64bits(ubuf, sizeof *ubuf);
    FSHCacheEntry* block = nullptr;
    for (int i = 0; i < FSH_MAX; i++) {
        if (gpu->fshaders.d[i].hash == hash || gpu->fshaders.d[i].hash == 0) {
            block = &gpu->fshaders.d[i];
            break;
        }
    }
    if (!block) {
        block = LRU_eject(gpu->fshaders);
    }
    LRU_use(gpu->fshaders, block);
    if (block->hash != hash) {
        block->hash = hash;
        glDeleteShader(block->fs);
        block->fs = glCreateShader(GL_FRAGMENT_SHADER);

        char* source = shader_gen_fs(ubuf);

        printf(source);

        glShaderSource(block->fs, 1, &(const char*) {source}, nullptr);
        free(source);

        glCompileShader(block->fs);
    }
    return block->fs;
}

char* fs_header = R"(
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

struct Light {
    vec3 specular0;
    vec3 specular1;
    vec3 diffuse;
    vec3 ambient;
    vec4 vec;
};

layout (std140) uniform FragUniforms {
    vec4 tev_color[6];
    vec4 tev_buffer_color;

    Light light[8];
    vec4 ambient_color;

    float alpharef;
};

vec3 quatrot(vec4 q, vec3 v) {
    return 2 * (q.w * cross(q.xyz, v) + q.xyz * dot(q.xyz, v)) +
           (q.w * q.w - dot(q.xyz, q.xyz)) * v;
}

)";

#define CHECKSRC(s)                                                            \
    ({                                                                         \
        if (s == TEVSRC_LIGHT_PRIMARY || s == TEVSRC_LIGHT_SECONDARY)          \
            lighting = true;                                                   \
        if (s == TEVSRC_TEX0) tex0 = true;                                     \
        if (s == TEVSRC_TEX1) tex1 = true;                                     \
        if (s == TEVSRC_TEX2) tex2 = true;                                     \
    })

const char* tevsrc_str(int i, u32 tevsrc) {
    switch (tevsrc) {
        case TEVSRC_COLOR:
            return "color";
        case TEVSRC_LIGHT_PRIMARY:
            return "light0";
        case TEVSRC_LIGHT_SECONDARY:
            return "light1";
        case TEVSRC_TEX0:
            return "tex0c";
        case TEVSRC_TEX1:
            return "tex1c";
        case TEVSRC_TEX2:
            return "tex2c";
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
                default: // unreachable
                    return "vec4(0)";
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
            ds_printf(s, "vec3(%s.a)", srcstr);
            break;
        case 3:
            ds_printf(s, "vec3(1 - %s.a)", srcstr);
            break;
        case 4:
            ds_printf(s, "vec3(%s.r)", srcstr);
            break;
        case 5:
            ds_printf(s, "vec3(1 - %s.r)", srcstr);
            break;
        case 8:
            ds_printf(s, "vec3(%s.g)", srcstr);
            break;
        case 9:
            ds_printf(s, "vec3(1 - %s.g)", srcstr);
            break;
        case 12:
            ds_printf(s, "vec3(%s.b)", srcstr);
            break;
        case 13:
            ds_printf(s, "vec3(1 - %s.b)", srcstr);
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

void write_combiner_rgb(DynString* s, UberUniforms* ubuf, int i) {
#define SRC(n)                                                                 \
    write_operand_rgb(s, tevsrc_str(i, ubuf->tev[i].rgb.src##n),               \
                      ubuf->tev[i].rgb.op##n)
    switch (ubuf->tev[i].rgb.combiner) {
        case 0:
            SRC(0);
            break;
        case 1:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            break;
        case 2:
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            break;
        case 3:
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, " - 0.5");
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
            SRC(0);
            ds_printf(s, " - ");
            SRC(1);
            break;
        case 6:
        case 7:
            ds_printf(s, "vec3(4 * dot(");
            SRC(0);
            ds_printf(s, " - 0.5, ");
            SRC(1);
            ds_printf(s, " - 0.5))");
            break;
        case 8:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, " + ");
            SRC(2);
            break;
        case 9:
            ds_printf(s, "(");
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, ") + ");
            SRC(2);
            break;
        default:
            SRC(0);
            break;
    }
#undef SRC
}

void write_combiner_a(DynString* s, UberUniforms* ubuf, int i) {
#define SRC(n)                                                                 \
    write_operand_a(s, tevsrc_str(i, ubuf->tev[i].a.src##n),                   \
                    ubuf->tev[i].a.op##n)
    switch (ubuf->tev[i].a.combiner) {
        case 0:
            SRC(0);
            break;
        case 1:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            break;
        case 2:
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            break;
        case 3:
            SRC(0);
            ds_printf(s, " + ");
            SRC(1);
            ds_printf(s, " - 0.5");
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
            SRC(0);
            ds_printf(s, " - ");
            SRC(1);
            break;
        case 6:
        case 7:
            ds_printf(s, "4 * (");
            SRC(0);
            ds_printf(s, " - 0.5) * (");
            SRC(1);
            ds_printf(s, " - 0.5)");
            break;
        case 8:
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, " + ");
            SRC(2);
            break;
        case 9:
            ds_printf(s, "(");
            SRC(0);
            ds_printf(s, " * ");
            SRC(1);
            ds_printf(s, ") + ");
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

char* shader_gen_fs(UberUniforms* ubuf) {
    DynString s;
    ds_init(&s, 8192);

    bool lighting = false;
    bool tex0 = false;
    bool tex1 = false;
    bool tex2 = false;

    for (int i = 0; i < 6; i++) {
        CHECKSRC(ubuf->tev[i].rgb.src0);
        CHECKSRC(ubuf->tev[i].rgb.src1);
        CHECKSRC(ubuf->tev[i].rgb.src2);
        CHECKSRC(ubuf->tev[i].a.src0);
        CHECKSRC(ubuf->tev[i].a.src1);
        CHECKSRC(ubuf->tev[i].a.src2);
    }

    ds_printf(&s, "%s", fs_header);

    ds_printf(&s, "void main() {\n");

    if (lighting) {
        // stub rn
        ds_printf(&s, "vec4 light0 = vec4(0.5);\n");
        ds_printf(&s, "vec4 light1 = vec4(vec3(0), 0.5);\n");
    }
    if (tex0) {
        ds_printf(&s, "vec4 tex0c = texture(tex0, texcoord0);\n");
    }
    if (tex1) {
        ds_printf(&s, "vec4 tex1c = texture(tex1, texcoord1);\n");
    }
    if (tex2) {
        ds_printf(&s, "vec4 tex2c = texture(tex2, texcoord%d);\n",
                  ubuf->tex2coord ? 1 : 2);
    }

    ds_printf(&s, "vec4 cur = vec4(0);\n");
    ds_printf(&s, "vec4 buf = tev_buffer_color;\n");
    ds_printf(&s, "vec4 tmp;\n");

    for (int i = 0; i < 6; i++) {
        // check for do nothing stage
        bool skiprgb = ubuf->tev[i].rgb.combiner == 0 &&
                       ubuf->tev[i].rgb.op0 == 0 &&
                       ubuf->tev[i].rgb.src0 == TEVSRC_PREVIOUS;
        bool skipa = ubuf->tev[i].a.combiner == 0 && ubuf->tev[i].a.op0 == 0 &&
                     ubuf->tev[i].a.src0 == TEVSRC_PREVIOUS;
        bool needsclamp = false; // dont clamp if we didnt do anything
        if (!skiprgb && !skipa) {
            ds_printf(&s, "tmp.rgb = ");
            write_combiner_rgb(&s, ubuf, i);
            ds_printf(&s, ";\n");
            if (ubuf->tev[i].rgb.combiner == 7) {
                ds_printf(&s, "tmp.a = tmp.r;\n");
            } else {
                ds_printf(&s, "tmp.a = ");
                write_combiner_a(&s, ubuf, i);
                ds_printf(&s, ";\n");
            }

            ds_printf(&s, "cur = tmp;\n");
            needsclamp = true;
        }
        if (ubuf->tev[i].rgb.scale != 1.0f) {
            ds_printf(&s, "cur.rgb *= %f;\n", ubuf->tev[i].rgb.scale);
            needsclamp = true;
        }
        if (ubuf->tev[i].a.scale != 1.0f) {
            ds_printf(&s, "cur.a *= %f;\n", ubuf->tev[i].a.scale);
            needsclamp = true;
        }
        if (needsclamp) ds_printf(&s, "cur = clamp(cur, 0, 1);\n");
        if (ubuf->tev_update_rgb & BIT(i)) {
            ds_printf(&s, "buf.rgb = cur.rgb;\n");
        }
        if (ubuf->tev_update_alpha & BIT(i)) {
            ds_printf(&s, "buf.a = cur.a;\n");
        }
    }

    ds_printf(&s, "fragclr = cur;\n");

    if (ubuf->alphatest) {
        ds_printf(&s, "if (!%s) discard;\n", alphatest_str(ubuf->alphafunc));
    }

    ds_printf(&s, "}\n");

    return s.str;
}