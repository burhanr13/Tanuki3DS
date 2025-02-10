#include "shaderdec.h"

#define XXH_INLINE_ALL
#include <xxh3.h>

#include <dynstring.h>

#include "gpu.h"
#include "renderer_gl.h"

int shader_dec_get(GPU* gpu) {
    // we need to hash the shader code, entrypoint, and outmap
    XXH3_state_t* xxst = XXH3_createState();
    XXH3_64bits_reset(xxst);
    XXH3_64bits_update(xxst, gpu->progdata, sizeof gpu->progdata);
    XXH3_64bits_update(xxst, &gpu->regs.vsh.entrypoint,
                       sizeof gpu->regs.vsh.entrypoint);
    XXH3_64bits_update(xxst, gpu->regs.raster.sh_outmap,
                       sizeof gpu->regs.raster.sh_outmap);
    u64 hash = XXH3_64bits_digest(xxst);
    XXH3_freeState(xxst);

    VSHCacheEntry* block = nullptr;
    for (int i = 0; i < VSH_MAX; i++) {
        if (gpu->vshaders_hw.d[i].hash == hash ||
            gpu->vshaders_hw.d[i].hash == 0) {
            block = &gpu->vshaders_hw.d[i];
            break;
        }
    }
    if (!block) {
        block = LRU_eject(gpu->vshaders_hw);
    }
    LRU_use(gpu->vshaders_hw, block);
    if (block->hash != hash) {
        block->hash = hash;
        glDeleteShader(block->vs);

        char* source = shader_dec_vs(gpu);

        block->vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(block->vs, 1, &(const char*) {source}, nullptr);
        glCompileShader(block->vs);

        printf(source);

        free(source);
    }
    return block->vs;
}

#define printf(...) ds_printf(&ctx->s, __VA_ARGS__)

typedef struct {
    Vector(PICAInstr) calls;
    u32 depth;
    u32 farthestjmp;
    DynString s;
    ShaderUnit shu;
} DecCTX;

void dec_block(DecCTX* ctx, u32 start, u32 num);

const char vs_header[] = R"(
#version 410 core

layout (location=0) in vec4 v0;
layout (location=1) in vec4 v1;
layout (location=2) in vec4 v2;
layout (location=3) in vec4 v3;
layout (location=4) in vec4 v4;
layout (location=5) in vec4 v5;
layout (location=6) in vec4 v6;
layout (location=7) in vec4 v7;
layout (location=8) in vec4 v8;
layout (location=9) in vec4 v9;
layout (location=10) in vec4 v10;
layout (location=11) in vec4 v11;

out vec4 color;
out vec2 texcoord0;
out vec2 texcoord1;
out vec2 texcoord2;
out float texcoordw;
out vec4 normquat;
out vec3 view;

vec4 o[16];

vec4 r[16];

ivec2 a;
int aL;
bvec2 cmp;

layout (std140) uniform VertUniforms {
    vec4 c[96];
    ivec4 i[4];
    int b_raw;
};

#define b(n) ((b_raw & (1 << n)) != 0)

)";

const char uniforms[] = R"(
uniform vec4 c[96];

)";

static char coordnames[4] = "xyzw";
static char* outmapnames[24] = {
    "pos.x",       "pos.y",      "pos.z",       "pos.w",       "normquat.x",
    "normquat.y",  "normquat.z", "normquat.w",  "color.r",     "color.g",
    "color.b",     "color.a",    "texcoord0.x", "texcoord0.y", "texcoord1.x",
    "texcoord1.y", "texcoordw",  "r[0].x",      "view.x",      "view.y",
    "view.z",      "r[0].x",     "texcoord2.x", "texcoord2.y",
};

static char* comparefuncs[6] = {"==", "!=", "<", "<=", ">", ">="};

void deccondop(DecCTX* ctx, u32 op, bool refx, bool refy) {
    switch (op) {
        case 0:
            printf("%scmp.x || %scmp.y", refx ? "" : "!", refy ? "" : "!");
            break;
        case 1:
            printf("%scmp.x && %scmp.y", refx ? "" : "!", refy ? "" : "!");
            break;
        case 2:
            printf("%scmp.x", refx ? "" : "!");
            break;
        case 3:
            printf("%scmp.y", refy ? "" : "!");
            break;
    }
}

void decsrc(DecCTX* ctx, u32 n, u8 idx, u8 swizzle, bool neg) {
    if (neg) printf("-");
    if (n < 0x10) {
        // pica only supports 12 vertex attributes
        // so we will also only have 12 vertex attributes
        if (n < 12) printf("v%d", n);
        else printf("vec4(0)");
    } else if (n < 0x20) printf("r[%d]", n - 0x10);
    else {
        n -= 0x20;
        if (idx) {
            printf("c[(%d + ", n);
            switch (idx) {
                case 1:
                    printf("a.x");
                    break;
                case 2:
                    printf("a.y");
                    break;
                case 3:
                    printf("aL");
                    break;
            }
            printf(") & 0x7f]");
        } else printf("c[%d]", n);
    }
    if (swizzle != 0b00011011) {
        printf(".");
        for (int i = 0; i < 4; i++) {
            printf("%c", coordnames[(swizzle >> 2 * (3 - i)) & 3]);
        }
    }
}

void decdest(DecCTX* ctx, u32 n, u8 mask) {
    if (mask == 0) return;
    if (n < 0x10) printf("o[%d]", n);
    else printf("r[%d]", n - 0x10);
    if (mask != 0b1111) {
        printf(".");
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) printf("%c", coordnames[i]);
        }
    }
    if (mask == 0b1111) printf(" = ");
    else printf(" = (");
}

void decdestend(DecCTX* ctx, u8 mask) {
    if (mask != 0b1111 && mask != 0) {
        printf(").");
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) printf("%c", coordnames[i]);
        }
    }
    printf(";\n");
}

#define SRC(i, _fmt)                                                           \
    decsrc(ctx, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                   \
           desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(fmt) SRC(1, fmt)
#define SRC2(fmt) SRC(2, fmt)
#define SRC3(fmt) SRC(3, fmt)
#define DEST(_fmt) decdest(ctx, instr.fmt##_fmt.dest, desc.destmask)
#define FIN(_fmt) decdestend(ctx, desc.destmask)

u32 dec_instr(DecCTX* ctx, u32 pc) {
    PICAInstr instr = ctx->shu.code[pc++];

    if (instr.opcode != PICA_NOP) {
        for (int i = 0; i < ctx->depth; i++) {
            printf("%4s", "");
        }
    }

    OpDesc desc = ctx->shu.opdescs[instr.desc];
    switch (instr.opcode) {
        case PICA_ADD:
            DEST(1);
            SRC1(1);
            printf(" + ");
            SRC2(1);
            FIN(1);
            break;
        case PICA_DP3:
            DEST(1);
            printf("vec4(dot(");
            SRC1(1);
            printf(".xyz, ");
            SRC2(1);
            printf(".xyz))");
            FIN(1);
            break;
        case PICA_DP4:
            DEST(1);
            printf("vec4(dot(");
            SRC1(1);
            printf(", ");
            SRC2(1);
            printf("))");
            FIN(1);
            break;
        case PICA_DPH:
        case PICA_DPHI:
            DEST(1);
            printf("vec4(dot(vec4(");
            if (instr.opcode == PICA_DPHI) SRC1(1i);
            else SRC1(1);
            printf(".xyz, 1), ");
            if (instr.opcode == PICA_DPHI) SRC2(1i);
            else SRC2(1);
            printf("))");
            FIN(1);
            break;
        case PICA_EX2:
            DEST(1);
            printf("vec4(exp2(");
            SRC1(1);
            printf(".x))");
            FIN(1);
            break;
        case PICA_LG2:
            DEST(1);
            printf("vec4(log2(");
            SRC1(1);
            printf(".x))");
            FIN(1);
            break;
        case PICA_MUL:
            DEST(1);
            SRC1(1);
            printf(" * ");
            SRC2(1);
            FIN(1);
            break;
            // case PICA_SGE:
            //     DISASMFMT1(sge);
            // case PICA_SLT:
            //     DISASMFMT1(slt);
            // case PICA_SGEI:
            //     DISASMFMT1I(sge);
            // case PICA_SLTI:
            //     DISASMFMT1I(slt);
            // case PICA_FLR:
            //     DISASMFMT1U(flr);
        case PICA_MAX:
            DEST(1);
            printf("max(");
            SRC1(1);
            printf(", ");
            SRC2(1);
            printf(")");
            FIN(1);
            break;
        case PICA_MIN:
            DEST(1);
            printf("min(");
            SRC1(1);
            printf(", ");
            SRC2(1);
            printf(")");
            FIN(1);
            break;
        case PICA_RCP:
            DEST(1);
            printf("vec4(1 / ");
            SRC1(1);
            printf(".x)");
            FIN(1);
            break;
        case PICA_RSQ:
            DEST(1);
            printf("vec4(1 / sqrt(");
            SRC1(1);
            printf(".x))");
            FIN(1);
            break;
        case PICA_MOVA:
            if (desc.destmask & 0b1100) {
                printf("a.");
                for (int i = 0; i < 2; i++) {
                    if (desc.destmask & BIT(3 - i)) {
                        printf("%c", coordnames[i]);
                    }
                }
                printf(" = ivec4(");
                SRC1(1);
                printf(").");
                for (int i = 0; i < 2; i++) {
                    if (desc.destmask & BIT(3 - i)) {
                        printf("%c", coordnames[i]);
                    }
                }
                printf(";\n");
            }
            break;
        case PICA_MOV:
            DEST(1);
            SRC1(1);
            FIN(1);
            break;
        case PICA_NOP:
            break;
        // case PICA_BREAK:
        //     printf("break");
        //     break;
        // case PICA_BREAKC:
        //     printf("breakc");
        //     disasmcondop(instr.fmt2.op, instr.fmt2.refx,
        //     instr.fmt2.refy); break;
        case PICA_END:
            printf("return;\n");
            if (ctx->farthestjmp < pc) pc = -1;
            break;
        case PICA_CALL:
        case PICA_CALLC:
        case PICA_CALLU: {
            if (instr.opcode != PICA_CALL) {
                printf("if (");
                if (instr.opcode == PICA_CALLC) {
                    deccondop(ctx, instr.fmt2.op, instr.fmt2.refx,
                              instr.fmt2.refy);
                } else {
                    printf("b(%d)", instr.fmt3.c);
                }
                printf(") ");
            }
            printf("proc_%03x();\n", instr.fmt2.dest);
            bool found = false;
            Vec_foreach(c, ctx->calls) {
                if (c->fmt2.dest == instr.fmt2.dest) {
                    found = true;
                    if (c->fmt2.num < instr.fmt2.num)
                        c->fmt2.num = instr.fmt2.num;
                }
            }
            if (!found) {
                Vec_push(ctx->calls, instr);
            }
            break;
        }
        case PICA_IFU:
        case PICA_IFC: {
            printf("if (");
            if (instr.opcode == PICA_IFU) {
                printf("b(%d)", instr.fmt3.c);
            } else {
                deccondop(ctx, instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
            }
            printf(") {\n");
            dec_block(ctx, pc, instr.fmt2.dest - pc);
            if (instr.fmt2.num) {
                for (int i = 0; i < ctx->depth; i++) {
                    printf("%4s", "");
                }
                printf("} else {\n");
                dec_block(ctx, instr.fmt2.dest, instr.fmt2.num);
            }
            for (int i = 0; i < ctx->depth; i++) {
                printf("%4s", "");
            }
            printf("}\n");
            pc = instr.fmt2.dest + instr.fmt2.num;
            break;
        }
        // case PICA_LOOP: {
        //     printf("loop i%d\n", instr.fmt3.c);
        //     disasm_block(shu, pc, instr.fmt3.dest + 1 - pc);
        //     printf("%14s", "");
        //     for (int i = 0; i < disasm.depth; i++) {
        //         printf("%4s", "");
        //     }
        //     printf("end loop");
        //     pc = instr.fmt3.dest + 1;
        //     break;
        // }
        // case PICA_JMPC:
        // case PICA_JMPU: {
        //     if (instr.opcode == PICA_JMPU) {
        //         printf("jmpu %s b%d, ", (instr.fmt2.num & 1) ? "not" :
        //         "",
        //                instr.fmt3.c);
        //     } else {
        //         printf("jmpc ");
        //         disasmcondop(instr.fmt2.op, instr.fmt2.refx,
        //         instr.fmt2.refy); printf(", ");
        //     }
        //     printf("%03x", instr.fmt2.dest);
        //     if (instr.fmt2.dest > disasm.farthestjmp)
        //         disasm.farthestjmp = instr.fmt2.dest;
        //     break;
        // }
        case PICA_CMP ... PICA_CMP + 1: {
            printf("cmp.x = ");
            if (instr.fmt1c.cmpx < 6) {
                SRC1(1c);
                printf(".x %s ", comparefuncs[instr.fmt1c.cmpx]);
                SRC2(1c);
                printf(".x");
            } else {
                printf("true");
            }
            printf(";\n");
            for (int i = 0; i < ctx->depth; i++) {
                printf("%4s", "");
            }
            printf("cmp.y = ");
            if (instr.fmt1c.cmpy < 6) {
                SRC1(1c);
                printf(".y");
                printf(" %s ", comparefuncs[instr.fmt1c.cmpy]);
                SRC2(1c);
                printf(".y");
            } else {
                printf("true");
            }
            printf(";\n");
            break;
        }
        case PICA_MAD ... PICA_MAD + 0xf: {
            desc = ctx->shu.opdescs[instr.fmt5.desc];
            DEST(5);
            SRC1(5);
            printf(" * ");
            if (instr.fmt5.opcode & 1) {
                SRC2(5);
                printf(" + ");
                SRC3(5);
            } else {
                SRC2(5i);
                printf(" + ");
                SRC3(5i);
            }
            FIN(5);
            break;
        }
        default:
            lerror("unknown pica instr for decompiler %08x (opcode=%x)",
                   instr.w, instr.opcode);
    }
    return pc;
}

void dec_block(DecCTX* ctx, u32 start, u32 num) {
    ctx->depth++;
    u32 end = SHADER_CODE_SIZE;
    if (start + num < end) end = start + num;
    for (u32 pc = start; pc < end;) {
        pc = dec_instr(ctx, pc);
    }
    ctx->depth--;
}

char* shader_dec_vs(GPU* gpu) {
    DynString final;
    ds_init(&final, 32768);

    ds_printf(&final, vs_header);

    DecCTX ctx = {};
    ds_init(&ctx.s, 32768);
    ctx.shu.code = (PICAInstr*) gpu->progdata;
    ctx.shu.opdescs = (OpDesc*) gpu->opdescs;
    ctx.shu.entrypoint = gpu->regs.vsh.entrypoint;

    ds_printf(&ctx.s, "void proc_main() {\n");
    dec_block(&ctx, ctx.shu.entrypoint, SHADER_CODE_SIZE);
    ds_printf(&ctx.s, "}\n");
    for (int i = 0; i < ctx.calls.size; i++) {
        u32 start = ctx.calls.d[i].fmt3.dest;
        u32 num = ctx.calls.d[i].fmt3.num;
        ds_printf(&final, "void proc_%03x();\n", start);
        ds_printf(&ctx.s, "void proc_%03x() {\n", start);
        dec_block(&ctx, start, num);
        ds_printf(&ctx.s, "}\n");
    }

    ds_printf(&final, "\n%s\n", ctx.s.str);
    free(ctx.s.str);

    ds_printf(&final, "void main() {\n");

    ds_printf(&final, "proc_main();\n");

    ds_printf(&final, "vec4 pos;\n");

    for (int o = 0; o < 7; o++) {
        u32 all = gpu->regs.raster.sh_outmap[o][0] << 24 |
                  gpu->regs.raster.sh_outmap[o][1] << 16 |
                  gpu->regs.raster.sh_outmap[o][2] << 8 |
                  gpu->regs.raster.sh_outmap[o][3];
        switch (all) {
            case 0x00'01'02'03:
                ds_printf(&final, "pos = o[%d];\n", o);
                break;
            case 0x04'05'06'07:
                ds_printf(&final, "normquat = o[%d];\n", o);
                break;
            case 0x08'09'0a'0b:
                ds_printf(&final, "color = o[%d];\n", o);
                break;
            case 0x0c'0d'1f'1f:
                ds_printf(&final, "texcoord0 = o[%d].xy;\n", o);
                break;
            case 0x0c'0d'10'1f:
                ds_printf(&final, "texcoord0 = o[%d].xy;\n", o);
                ds_printf(&final, "texcoordw = o[%d].z;\n", o);
                break;
            case 0x0e'0f'1f'1f:
                ds_printf(&final, "texcoord1 = o[%d].xy;\n", o);
                break;
            case 0x12'13'14'1f:
                ds_printf(&final, "view = o[%d].xyz;\n", o);
                break;
            case 0x16'17'1f'1f:
                ds_printf(&final, "texcoord2 = o[%d].xy;\n", o);
                break;
            default:
                for (int i = 0; i < 4; i++) {
                    int sem = gpu->regs.raster.sh_outmap[o][i];
                    if (sem < 0x18)
                        ds_printf(&final, "%s = o[%d].%c;\n", outmapnames[sem],
                                  o, coordnames[i]);
                }
        }
    }

    // correct z value
    ds_printf(&final, "pos.z = pos.z * 2 + pos.w;\n");
    ds_printf(&final, "gl_Position = pos;\n");

    ds_printf(&final, "}\n");

    return final.str;
}