#include "shaderdec.h"

#include "emulator.h"

#include "gpu.h"
#include "gpu_hash.h"
#include "renderer_gl.h"

// #define VSH_DEBUG

typedef struct {
    Vector(PICAInstr) calls;
    u32 depth;
    u32 farthestjmp;
    FILE* s;
    ShaderUnit shu;
    u32 curfuncstart;
    u32 curfuncend;
    u32 curblockstart;
    u32 curblockend;
    int out_view; // for freecam
} DecCTX;

void dec_block(DecCTX* ctx, u32 start, u32 num);

const char vs_header[] = R"(
#version 330 core

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
uint aL;
bvec2 cmp;

layout (std140) uniform VertUniforms {
    vec4 c[96];
    uvec4 i[4];
    uint b_raw;
};

uniform float depthOffset;
uniform float depthScale;
uniform bool depthWBuffer;

#define b(n) ((b_raw & (1u << n)) != 0u)

layout (std140) uniform FreecamUniforms {
    mat4 freecam_mtx;
    bool freecam_enable;
};

#define SAFEMUL(a,b) (a * mix(vec4(0),b,notEqual(a,vec4(0))))

)";

static char coordnames[4] = "xyzw";
static char* outmapnames[24] = {
    "pos.x",       "pos.y",      "pos.z",       "pos.w",       "normquat.x",
    "normquat.y",  "normquat.z", "normquat.w",  "color.r",     "color.g",
    "color.b",     "color.a",    "texcoord0.x", "texcoord0.y", "texcoord1.x",
    "texcoord1.y", "texcoordw",  "r[0].x",      "view.x",      "view.y",
    "view.z",      "r[0].x",     "texcoord2.x", "texcoord2.y",
};

static char* comparefuncs[6] = {"equal",       "notEqual",
                                "lessThan",    "lessThanEqual",
                                "greaterThan", "greaterThanEqual"};
static char* compareops[6] = {"==", "!=", "<", "<=", ">", ">="};

void deccondop(DecCTX* ctx, u32 op, bool refx, bool refy) {
    switch (op) {
        case 0:
            if (refx == refy) { // special faster cases
                if (refx) {     // or
                    fprintf(ctx->s, "any(cmp)");
                } else { // nand
                    fprintf(ctx->s, "!all(cmp)");
                }
            } else {
                fprintf(ctx->s, "%scmp.x || %scmp.y", refx ? "" : "!",
                          refy ? "" : "!");
            }
            break;
        case 1:
            if (refx == refy) {
                if (refx) { // and
                    fprintf(ctx->s, "all(cmp)");
                } else { // nor
                    fprintf(ctx->s, "!any(cmp)");
                }
            } else {
                fprintf(ctx->s, "%scmp.x && %scmp.y", refx ? "" : "!",
                          refy ? "" : "!");
            }
            break;
        case 2:
            fprintf(ctx->s, "%scmp.x", refx ? "" : "!");
            break;
        case 3:
            fprintf(ctx->s, "%scmp.y", refy ? "" : "!");
            break;
    }
}

void decsrc(DecCTX* ctx, u32 n, u8 idx, u8 swizzle, bool neg) {
    if (neg) fprintf(ctx->s, "-");
    if (n < 0x10) {
        // pica only supports 12 vertex attributes
        // so we will also only have 12 vertex attributes
        if (n < 12) fprintf(ctx->s, "v%d", n);
        else fprintf(ctx->s, "vec4(0)");
    } else if (n < 0x20) fprintf(ctx->s, "r[%d]", n - 0x10);
    else {
        n -= 0x20;
        if (idx) {
            fprintf(ctx->s, "c[(%d + ", n);
            switch (idx) {
                case 1:
                    fprintf(ctx->s, "a.x");
                    break;
                case 2:
                    fprintf(ctx->s, "a.y");
                    break;
                case 3:
                    fprintf(ctx->s, "int(aL)");
                    break;
            }
            fprintf(ctx->s, ") & 0x7f]");
        } else fprintf(ctx->s, "c[%d]", n);
    }
    if (swizzle != 0b00011011) {
        fprintf(ctx->s, ".");
        for (int i = 0; i < 4; i++) {
            fprintf(ctx->s, "%c", coordnames[(swizzle >> 2 * (3 - i)) & 3]);
        }
    }
}

void decdest(DecCTX* ctx, u32 n, u8 mask) {
    if (mask == 0) return;
    if (n < 0x10) fprintf(ctx->s, "o[%d]", n);
    else fprintf(ctx->s, "r[%d]", n - 0x10);
    if (mask != 0b1111) {
        fprintf(ctx->s, ".");
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) fprintf(ctx->s, "%c", coordnames[i]);
        }
    }
    if (mask == 0b1111) fprintf(ctx->s, " = ");
    else fprintf(ctx->s, " = (");
}

void decdestend(DecCTX* ctx, u8 mask) {
    if (mask != 0b1111 && mask != 0) {
        fprintf(ctx->s, ").");
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) fprintf(ctx->s, "%c", coordnames[i]);
        }
    }
    fprintf(ctx->s, ";\n");
}

#define SRC(i, _fmt)                                                           \
    decsrc(ctx, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                   \
           desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(fmt) SRC(1, fmt)
#define SRC2(fmt) SRC(2, fmt)
#define SRC3(fmt) SRC(3, fmt)
#define DEST(_fmt) decdest(ctx, instr.fmt##_fmt.dest, desc.destmask)
#define FIN(_fmt) decdestend(ctx, desc.destmask)

#define INDENT(n)                                                              \
    ({                                                                         \
        for (int i = 0; i < (n); i++) {                                        \
            fprintf(ctx->s, "%4s", "");                                     \
        }                                                                      \
    })

bool contains_jmp_out(DecCTX* ctx, u32 start, u32 end) {
    for (int pc = start; pc < end; pc++) {
        PICAInstr instr = ctx->shu.code[pc];
        if (instr.opcode == PICA_JMPC || instr.opcode == PICA_JMPU) {
            u32 dst = instr.fmt2.dest;
            if (dst < start || dst > end) return true;
        }
    }
    return false;
}

u32 dec_instr(DecCTX* ctx, u32 pc) {
    PICAInstr instr = ctx->shu.code[pc++];

    INDENT(ctx->depth);

    OpDesc desc = ctx->shu.opdescs[instr.desc];
    switch (instr.opcode) {
        case PICA_ADD:
            DEST(1);
            SRC1(1);
            fprintf(ctx->s, " + ");
            SRC2(1);
            FIN(1);
            break;
        case PICA_DP3:
            DEST(1);
            if (ctremu.safeShaderMul) {
                fprintf(ctx->s, "vec4(dot(SAFEMUL(");
                SRC1(1);
                fprintf(ctx->s, ", ");
                SRC2(1);
                fprintf(ctx->s, ").xyz, vec3(1)))");
            } else {
                fprintf(ctx->s, "vec4(dot(");
                SRC1(1);
                fprintf(ctx->s, ".xyz, ");
                SRC2(1);
                fprintf(ctx->s, ".xyz))");
            }
            FIN(1);
            break;
        case PICA_DP4:
            DEST(1);
            if (ctremu.safeShaderMul) {
                fprintf(ctx->s, "vec4(dot(SAFEMUL(");
                SRC1(1);
                fprintf(ctx->s, ", ");
                SRC2(1);
                fprintf(ctx->s, "), vec4(1)))");
            } else {
                fprintf(ctx->s, "vec4(dot(");
                SRC1(1);
                fprintf(ctx->s, ", ");
                SRC2(1);
                fprintf(ctx->s, "))");
            }
            FIN(1);
            break;
        case PICA_DPH:
        case PICA_DPHI:
            DEST(1);
            if (ctremu.safeShaderMul) {
                fprintf(ctx->s, "vec4(dot(SAFEMUL(vec4(");
                if (instr.opcode == PICA_DPHI) SRC1(1i);
                else SRC1(1);
                fprintf(ctx->s, ".xyz, 1), ");
                if (instr.opcode == PICA_DPHI) SRC2(1i);
                else SRC2(1);
                fprintf(ctx->s, "), vec4(1)))");
            } else {
                fprintf(ctx->s, "vec4(dot(vec4(");
                if (instr.opcode == PICA_DPHI) SRC1(1i);
                else SRC1(1);
                fprintf(ctx->s, ".xyz, 1), ");
                if (instr.opcode == PICA_DPHI) SRC2(1i);
                else SRC2(1);
                fprintf(ctx->s, "))");
            }
            FIN(1);
            break;
        case PICA_EX2:
            DEST(1);
            fprintf(ctx->s, "vec4(exp2(");
            SRC1(1);
            fprintf(ctx->s, ".x))");
            FIN(1);
            break;
        case PICA_LG2:
            DEST(1);
            fprintf(ctx->s, "vec4(log2(");
            SRC1(1);
            fprintf(ctx->s, ".x))");
            FIN(1);
            break;
        case PICA_MUL:
            DEST(1);
            if (ctremu.safeShaderMul) {
                fprintf(ctx->s, "SAFEMUL(");
                SRC1(1);
                fprintf(ctx->s, ", ");
                SRC2(1);
                fprintf(ctx->s, ")");
            } else {
                SRC1(1);
                fprintf(ctx->s, " * ");
                SRC2(1);
            }
            FIN(1);
            break;
        case PICA_SGE:
        case PICA_SGEI:
            DEST(1);
            fprintf(ctx->s, "vec4(greaterThanEqual(");
            if (instr.opcode == PICA_SGEI) SRC1(1i);
            else SRC1(1);
            fprintf(ctx->s, ", ");
            if (instr.opcode == PICA_SGEI) SRC2(1i);
            else SRC2(1);
            fprintf(ctx->s, "))");
            FIN(1);
            break;
        case PICA_SLT:
        case PICA_SLTI:
            DEST(1);
            fprintf(ctx->s, "vec4(lessThan(");
            if (instr.opcode == PICA_SLTI) SRC1(1i);
            else SRC1(1);
            fprintf(ctx->s, ", ");
            if (instr.opcode == PICA_SLTI) SRC2(1i);
            else SRC2(1);
            fprintf(ctx->s, "))");
            FIN(1);
            break;
        case PICA_FLR:
            DEST(1);
            fprintf(ctx->s, "floor(");
            SRC1(1);
            fprintf(ctx->s, ")");
            FIN(1);
            break;
        case PICA_MAX:
            DEST(1);
            fprintf(ctx->s, "max(");
            SRC1(1);
            fprintf(ctx->s, ", ");
            SRC2(1);
            fprintf(ctx->s, ")");
            FIN(1);
            break;
        case PICA_MIN:
            DEST(1);
            fprintf(ctx->s, "min(");
            SRC1(1);
            fprintf(ctx->s, ", ");
            SRC2(1);
            fprintf(ctx->s, ")");
            FIN(1);
            break;
        case PICA_RCP:
            DEST(1);
            fprintf(ctx->s, "vec4(1 / ");
            SRC1(1);
            fprintf(ctx->s, ".x)");
            FIN(1);
            break;
        case PICA_RSQ:
            DEST(1);
            fprintf(ctx->s, "vec4(inversesqrt(");
            SRC1(1);
            fprintf(ctx->s, ".x))");
            FIN(1);
            break;
        case PICA_MOVA:
            if (desc.destmask & 0b1100) {
                fprintf(ctx->s, "a.");
                for (int i = 0; i < 2; i++) {
                    if (desc.destmask & BIT(3 - i)) {
                        fprintf(ctx->s, "%c", coordnames[i]);
                    }
                }
                fprintf(ctx->s, " = ivec4(clamp(");
                SRC1(1);
                fprintf(ctx->s, ", -128, 127)).");
                for (int i = 0; i < 2; i++) {
                    if (desc.destmask & BIT(3 - i)) {
                        fprintf(ctx->s, "%c", coordnames[i]);
                    }
                }
                fprintf(ctx->s, ";\n");
            }
            break;
        case PICA_MOV:
            if (instr.fmt1.dest == ctx->out_view) {
                u32 rn = instr.fmt1.src1 - 0x10;
                if (rn < 0x10) {
                    fprintf(ctx->s,
                              "if (freecam_enable) r[%d] = freecam_mtx * "
                              "r[%d];\n",
                              rn, rn);
                    INDENT(ctx->depth);
                }
            }
            DEST(1);
            SRC1(1);
            FIN(1);
            break;
        case PICA_NOP:
            fprintf(ctx->s, "\n");
            break;
        case PICA_BREAK:
            fprintf(ctx->s, "break;\n");
            break;
        case PICA_BREAKC:
            fprintf(ctx->s, "if (");
            deccondop(ctx, instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
            fprintf(ctx->s, ") break;\n");
            break;
        case PICA_END:
            fprintf(ctx->s, "return;\n");
            if (ctx->farthestjmp < pc) pc = -1;
            break;
        case PICA_CALL:
        case PICA_CALLC:
        case PICA_CALLU: {
            if (instr.opcode != PICA_CALL) {
                fprintf(ctx->s, "if (");
                if (instr.opcode == PICA_CALLC) {
                    deccondop(ctx, instr.fmt2.op, instr.fmt2.refx,
                              instr.fmt2.refy);
                } else {
                    fprintf(ctx->s, "b(%d)", instr.fmt3.c);
                }
                fprintf(ctx->s, ") ");
            }
            fprintf(ctx->s, "proc_%03x();\n", instr.fmt2.dest);

            bool found = false;
            Vec_foreach(c, ctx->calls) {
                if (c->fmt2.dest == instr.fmt2.dest) {
                    found = true;
                    if (c->fmt2.num != instr.fmt2.num)
                        lerror("calling function with different length");
                }
            }
            if (!found) {
                Vec_push(ctx->calls, instr);
            }
            break;
        }
        case PICA_IFU:
        case PICA_IFC: {
            fprintf(ctx->s, "if (");
            if (instr.opcode == PICA_IFU) {
                fprintf(ctx->s, "b(%d)", instr.fmt3.c);
            } else {
                deccondop(ctx, instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
            }
            fprintf(ctx->s, ") {\n");
            dec_block(ctx, pc, instr.fmt2.dest - pc);
            if (instr.fmt2.num) {
                INDENT(ctx->depth);
                fprintf(ctx->s, "} else {\n");
                dec_block(ctx, instr.fmt2.dest, instr.fmt2.num);
            }
            INDENT(ctx->depth);
            fprintf(ctx->s, "}\n");
            pc = instr.fmt2.dest + instr.fmt2.num;
            break;
        }
        case PICA_LOOP: {
            fprintf(ctx->s, "aL = i[%d].y;\n", instr.fmt3.c);
            INDENT(ctx->depth);
            fprintf(ctx->s,
                      "for (uint l = 0u; l <= i[%d].x; l++, aL = (aL + "
                      "i[%d].z) & 0xffu) {\n",
                      instr.fmt3.c, instr.fmt3.c);
            dec_block(ctx, pc, instr.fmt3.dest + 1 - pc);
            INDENT(ctx->depth);
            fprintf(ctx->s, "}\n");
            pc = instr.fmt3.dest + 1;
            break;
        }
        case PICA_JMPC:
        case PICA_JMPU: {
            u32 dst = instr.fmt2.dest;

            // jmp to next instr - nop
            if (dst == pc) {
                fprintf(ctx->s, "\n");
                break;
            }
            // loop - handle this later
            if (dst < pc) {
                lerror("backwards jmp");
                break;
            }

            // jumping out of the block
            // if the block was from control flow, this is not supposed to
            // happen
            // if its a block from a prior jmp, then we have ensured already
            // that this is a function return
            if (dst > ctx->curblockend) {
                fprintf(ctx->s, "if (");
                if (instr.opcode == PICA_JMPC) {
                    deccondop(ctx, instr.fmt2.op, instr.fmt2.refx,
                              instr.fmt2.refy);
                } else {
                    fprintf(ctx->s, "%sb(%d)", instr.fmt3.num & 1 ? "!" : "",
                              instr.fmt3.c);
                }
                fprintf(ctx->s, ") {\n");
                dec_block(ctx, dst, ctx->curfuncend - dst);
                INDENT(ctx->depth + 1);
                fprintf(ctx->s, "return;\n");
                INDENT(ctx->depth);
                fprintf(ctx->s, "}\n");
            } else {
                // jmp within the same block
                // we need to check here if there is a jmp out of the block
                // right now we only let this happen if we are in the toplevel
                // block of a function
                bool jmpout = contains_jmp_out(ctx, pc, dst);

                if (!jmpout ||
                    (jmpout || ctx->curblockstart == ctx->curfuncstart)) {
                    // treat the jmp as an if statement
                    // if condition is inverse of jmp condition
                    fprintf(ctx->s, "if (!(");
                    if (instr.opcode == PICA_JMPC) {
                        deccondop(ctx, instr.fmt2.op, instr.fmt2.refx,
                                  instr.fmt2.refy);
                    } else {
                        fprintf(ctx->s, "%sb(%d)",
                                  instr.fmt3.num & 1 ? "!" : "", instr.fmt3.c);
                    }
                    fprintf(ctx->s, ")) {\n");
                    dec_block(ctx, pc, dst - pc);
                    INDENT(ctx->depth);
                    fprintf(ctx->s, "}\n");
                    pc = dst;
                } else {
                    lerror("unhandled control flow");
                }
            }

            if (instr.fmt2.dest > ctx->farthestjmp)
                ctx->farthestjmp = instr.fmt2.dest;
            break;
        }
        case PICA_CMP ... PICA_CMP + 1: {
            // for vector we need the function but for scalars the operator :/
            if (instr.fmt1c.cmpx == instr.fmt1c.cmpy) {
                fprintf(ctx->s, "cmp = ");
                if (instr.fmt1c.cmpx < 6) {
                    fprintf(ctx->s, "%s(", comparefuncs[instr.fmt1c.cmpx]);
                    SRC1(1c);
                    fprintf(ctx->s, ".xy, ");
                    SRC2(1c);
                    fprintf(ctx->s, ".xy)");
                } else {
                    fprintf(ctx->s, "bvec2(true)");
                }
                fprintf(ctx->s, ";\n");
            } else {
                fprintf(ctx->s, "cmp.x = ");
                if (instr.fmt1c.cmpx < 6) {
                    SRC1(1c);
                    fprintf(ctx->s, ".x %s ", compareops[instr.fmt1c.cmpx]);
                    SRC2(1c);
                    fprintf(ctx->s, ".x");
                } else {
                    fprintf(ctx->s, "true");
                }
                fprintf(ctx->s, ";\n");
                INDENT(ctx->depth);
                fprintf(ctx->s, "cmp.y = ");
                if (instr.fmt1c.cmpy < 6) {
                    SRC1(1c);
                    fprintf(ctx->s, ".y %s ", compareops[instr.fmt1c.cmpy]);
                    SRC2(1c);
                    fprintf(ctx->s, ".y");
                } else {
                    fprintf(ctx->s, "true");
                }
                fprintf(ctx->s, ";\n");
            }
            break;
        }
        case PICA_MAD ... PICA_MAD + 0xf: {
            desc = ctx->shu.opdescs[instr.fmt5.desc];
            DEST(5);
            if (ctremu.safeShaderMul) {
                fprintf(ctx->s, "SAFEMUL(");
                SRC1(5);
                fprintf(ctx->s, ", ");
                if (instr.fmt5.opcode & 1) {
                    SRC2(5);
                    fprintf(ctx->s, ") + ");
                    SRC3(5);
                } else {
                    SRC2(5i);
                    fprintf(ctx->s, ") + ");
                    SRC3(5i);
                }
            } else {
                SRC1(5);
                fprintf(ctx->s, " * ");
                if (instr.fmt5.opcode & 1) {
                    SRC2(5);
                    fprintf(ctx->s, " + ");
                    SRC3(5);
                } else {
                    SRC2(5i);
                    fprintf(ctx->s, " + ");
                    SRC3(5i);
                }
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

#undef printf

void dec_block(DecCTX* ctx, u32 start, u32 num) {
    ctx->curblockstart = start;
    ctx->curblockend = start + num;
    ctx->depth++;
    u32 end = SHADER_CODE_SIZE;
    if (start + num < end) end = start + num;
    for (u32 pc = start; pc < end;) {
        pc = dec_instr(ctx, pc);
    }
    ctx->depth--;
}

char* shader_dec_vs(GPU* gpu) {
    char* buf;
    size_t bufsz;
    FILE* final = open_memstream(&buf, &bufsz);

    fprintf(final, vs_header);

    char* codebuf;
    size_t codebufsz;
    DecCTX ctx = {};
    ctx.s = open_memstream(&codebuf, &codebufsz);
    ctx.shu.code = (PICAInstr*) gpu->vsh.progdata;
    ctx.shu.opdescs = (OpDesc*) gpu->vsh.opdescs;
    ctx.shu.entrypoint = gpu->regs.vsh.entrypoint;

    ctx.out_view = -1;
    for (int o = 0; o < 7; o++) {
        if (gpu->regs.raster.sh_outmap[o][0] == 0x12 &&
            gpu->regs.raster.sh_outmap[o][1] == 0x13 &&
            gpu->regs.raster.sh_outmap[o][2] == 0x14 &&
            gpu->regs.raster.sh_outmap[o][3] == 0x1f) {
            ctx.out_view = o;
        }
    }

    fprintf(ctx.s, "void proc_main() {\n");
    ctx.curfuncstart = ctx.shu.entrypoint;
    ctx.curfuncend = ctx.curfuncstart + SHADER_CODE_SIZE;
    dec_block(&ctx, ctx.shu.entrypoint, SHADER_CODE_SIZE);
    fprintf(ctx.s, "}\n\n");
    for (int i = 0; i < ctx.calls.size; i++) {
        u32 start = ctx.calls.d[i].fmt3.dest;
        u32 num = ctx.calls.d[i].fmt3.num;
        fprintf(final, "void proc_%03x();\n", start);
        fprintf(ctx.s, "void proc_%03x() {\n", start);
        ctx.curfuncstart = start;
        ctx.curfuncend = start + num;
        dec_block(&ctx, start, num);
        fprintf(ctx.s, "}\n\n");
    }
    Vec_free(ctx.calls);

    fclose(ctx.s);
    fprintf(final, "\n%s", codebuf);
    free(codebuf);

    fprintf(final, "void main() {\n");

    fprintf(final, "proc_main();\n\n");

    fprintf(final, "vec4 pos = vec4(1);\n");
    // macos gets mad if you dont write all the outputs
    // so we do that first
    fprintf(final, "color = vec4(1);\n");
    fprintf(final, "normquat = vec4(1);\n");
    fprintf(final, "view = vec3(1);\n");
    fprintf(final, "texcoord0 = vec2(1);\n");
    fprintf(final, "texcoord1 = vec2(1);\n");
    fprintf(final, "texcoord2 = vec2(1);\n");
    fprintf(final, "texcoordw = 1;\n\n");

    // handle the outmap mask
    int dstidx = 0;
    for (int i = 0; i < 16; i++) {
        if (!(gpu->regs.vsh.outmap_mask & BIT(i))) continue;
        if (dstidx != i) fprintf(final, "o[%d] = o[%d];\n", dstidx, i);
        dstidx++;
    }

    // handle semantics multiple times
    u32 written = 0;
    for (int o = 0; o < 7; o++) {
        u32 all = gpu->regs.raster.sh_outmap[o][0] << 24 |
                  gpu->regs.raster.sh_outmap[o][1] << 16 |
                  gpu->regs.raster.sh_outmap[o][2] << 8 |
                  gpu->regs.raster.sh_outmap[o][3];
        switch (all) {
            case 0x00'01'02'03:
                if (!(written & 0xf)) fprintf(final, "pos = o[%d];\n", o);
                written |= 0xf;
                break;
            case 0x04'05'06'07:
                if (!(written & (0xf << 4)))
                    fprintf(final, "normquat = o[%d];\n", o);
                written |= 0xf << 4;
                break;
            case 0x08'09'0a'0b:
                if (!(written & (0xf << 8)))
                    fprintf(final, "color = o[%d];\n", o);
                written |= 0xf << 8;
                break;
            case 0x0c'0d'1f'1f:
                if (!(written & (3 << 12)))
                    fprintf(final, "texcoord0 = o[%d].xy;\n", o);
                written |= 3 << 12;
                break;
            case 0x0c'0d'10'1f:
                if (!(written & (3 << 12)))
                    fprintf(final, "texcoord0 = o[%d].xy;\n", o);
                if (!(written & (1 << 16)))
                    fprintf(final, "texcoordw = o[%d].z;\n", o);
                written |= 3 << 12 | 1 << 16;
                break;
            case 0x0e'0f'1f'1f:
                if (!(written & (3 << 14)))
                    fprintf(final, "texcoord1 = o[%d].xy;\n", o);
                written |= 3 << 14;
                break;
            case 0x12'13'14'1f:
                if (!(written & (7 << 18)))
                    fprintf(final, "view = o[%d].xyz;\n", o);
                written |= 7 << 18;
                break;
            case 0x16'17'1f'1f:
                if (!(written & (3 << 22)))
                    fprintf(final, "texcoord2 = o[%d].xy;\n", o);
                written |= 3 << 22;
                break;
            default:
                for (int i = 0; i < 4; i++) {
                    int sem = gpu->regs.raster.sh_outmap[o][i];
                    if (sem < 0x18 && !(written & BIT(sem)))
                        fprintf(final, "%s = o[%d].%c;\n", outmapnames[sem],
                                  o, coordnames[i]);
                    written |= BIT(sem);
                }
        }
    }

    // depth map
    fprintf(final, R"(
pos.z = pos.z * depthScale + pos.w * depthOffset;
if (depthWBuffer) pos.z *= pos.w;
pos.z = 2 * pos.z - pos.w;
)");
    fprintf(final, "gl_Position = pos;\n");

    fprintf(final, "}\n");

    fclose(final);
    
#ifdef VSH_DEBUG
    pica_shader_disasm(&ctx.shu);
    fprintf(ctx->s, buf);
#endif

    return buf;
}
