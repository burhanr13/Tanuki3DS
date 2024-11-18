#include "shader.h"

#include <math.h>

void readsrc(ShaderUnit* shu, fvec src, u32 n, u8 idx, u8 swizzle, bool neg) {
    fvec* rn;
    if (n < 0x10) rn = &shu->v[n];
    else if (n < 0x20) rn = &shu->r[n - 0x10];
    else {
        n -= 0x20;
        if (idx) {
            switch (idx) {
                case 1:
                    n += shu->a[0];
                    break;
                case 2:
                    n += shu->a[1];
                    break;
                case 3:
                    n += shu->al;
                    break;
            }
            n &= 0x7f;
            if (n < 0x60) {
                rn = &shu->c[n];
            } else {
                static fvec dummy = {1, 1, 1, 1};
                rn = &dummy;
            }
        } else rn = &shu->c[n];
    }

    for (int i = 0; i < 4; i++) {
        src[i] = (*rn)[(swizzle >> 2 * (3 - i)) & 3];
        if (neg) src[i] = -src[i];
    }
}

void writedest(ShaderUnit* shu, fvec dest, u32 n, u8 mask) {
    fvec* rn;
    if (n < 0x10) rn = &shu->o[n];
    else rn = &shu->r[n - 0x10];
    for (int i = 0; i < 4; i++) {
        if (mask & BIT(3 - i)) (*rn)[i] = dest[i];
    }
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(shu, v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,               \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(v, fmt) SRC(v, 1, fmt)
#define SRC2(v, fmt) SRC(v, 2, fmt)
#define SRC3(v, fmt) SRC(v, 3, fmt)
#define DEST(v, _fmt) writedest(shu, v, instr.fmt##_fmt.dest, desc.destmask)

static inline bool condop(u32 op, bool x, bool y, bool refx, bool refy) {
    switch (op) {
        case 0:
            return x == refx || y == refy;
        case 1:
            return x == refx && y == refy;
        case 2:
            return x == refx;
        case 3:
            return y == refy;
        default:
            return true;
    }
}

static inline bool compare(u32 op, float a, float b) {
    switch (op) {
        case 0:
            return a == b;
        case 1:
            return a != b;
        case 2:
            return a < b;
        case 3:
            return a <= b;
        case 4:
            return a > b;
        case 5:
            return a >= b;
        default:
            return true;
    }
}

#define MUL(a, b) ((a != 0) ? a * b : 0)
#define MAX(a, b) (isinf(b) ? b : fmaxf(b, a))
#define MIN(a, b) (fminf(b, a))

typedef struct {
    u32 pc;
    u32 dest;
    bool loop;
    u32 idx;
    u32 inc;
    u32 max;
} Control;

#define PUSH() (sp = (sp + 1) % 16)
#define POP() (sp = (sp - 1) % 16)
#define TOP (stack[sp])

void shader_run(ShaderUnit* shu) {
    u32 pc = shu->entrypoint;

    Control stack[16];
    u32 sp = 0;
    TOP.pc = -1;
    TOP.dest = 0;

    while (pc < SHADER_CODE_SIZE) {
        PICAInstr instr = shu->code[pc++ % SHADER_CODE_SIZE];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = a[0] + b[0];
                res[1] = a[1] + b[1];
                res[2] = a[2] + b[2];
                res[3] = a[3] + b[3];
                DEST(res, 1);
                break;
            }
            case PICA_DP3: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = MUL(a[0], b[0]) + MUL(a[1], b[1]) + MUL(a[2], b[2]);
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_DP4: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = MUL(a[0], b[0]) + MUL(a[1], b[1]) + MUL(a[2], b[2]) +
                         MUL(a[3], b[3]);
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_DST:
            case PICA_DSTI: {
                fvec a, b;
                if (instr.opcode == PICA_DST) {
                    SRC1(a, 1);
                    SRC2(b, 1);
                } else {
                    SRC1(a, 1i);
                    SRC2(b, 1i);
                }
                fvec res;
                res[0] = 1;
                res[1] = MUL(a[1], b[1]);
                res[2] = a[2];
                res[3] = b[3];
                DEST(res, 1);
                break;
            }
            case PICA_EX2: {
                fvec a;
                SRC1(a, 1);
                fvec res;
                res[0] = exp2f(a[0]);
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_LG2: {
                fvec a;
                SRC1(a, 1);
                fvec res;
                res[0] = log2f(a[0]);
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_MUL: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = MUL(a[0], b[0]);
                res[1] = MUL(a[1], b[1]);
                res[2] = MUL(a[2], b[2]);
                res[3] = MUL(a[3], b[3]);
                DEST(res, 1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                fvec a, b;
                if (instr.opcode == PICA_SGE) {
                    SRC1(a, 1);
                    SRC2(b, 1);
                } else {
                    SRC1(a, 1i);
                    SRC2(b, 1i);
                }
                fvec res;
                res[0] = a[0] >= b[0];
                res[1] = a[1] >= b[1];
                res[2] = a[2] >= b[2];
                res[3] = a[3] >= b[3];
                DEST(res, 1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                fvec a, b;
                if (instr.opcode == PICA_SLT) {
                    SRC1(a, 1);
                    SRC2(b, 1);
                } else {
                    SRC1(a, 1i);
                    SRC2(b, 1i);
                }
                fvec res;
                res[0] = a[0] < b[0];
                res[1] = a[1] < b[1];
                res[2] = a[2] < b[2];
                res[3] = a[3] < b[3];
                DEST(res, 1);
                break;
            }
            case PICA_FLR: {
                fvec a;
                SRC1(a, 1);
                fvec res;
                res[0] = floorf(a[0]);
                res[1] = floorf(a[1]);
                res[2] = floorf(a[2]);
                res[3] = floorf(a[3]);
                DEST(res, 1);
                break;
            }
            case PICA_MAX: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = MAX(a[0], b[0]);
                res[1] = MAX(a[1], b[1]);
                res[2] = MAX(a[2], b[2]);
                res[3] = MAX(a[3], b[3]);
                DEST(res, 1);
                break;
            }
            case PICA_MIN: {
                fvec a, b;
                SRC1(a, 1);
                SRC2(b, 1);
                fvec res;
                res[0] = MIN(a[0], b[0]);
                res[1] = MIN(a[1], b[1]);
                res[2] = MIN(a[2], b[2]);
                res[3] = MIN(a[3], b[3]);
                DEST(res, 1);
                break;
            }
            case PICA_RCP: {
                fvec a;
                SRC1(a, 1);
                fvec res;
                if (a[0] == -0.f) a[0] = 0;
                res[0] = 1 / a[0];
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_RSQ: {
                fvec a;
                SRC1(a, 1);
                fvec res;
                if (a[0] == -0.f) a[0] = 0;
                res[0] = 1 / sqrtf(a[0]);
                res[1] = res[2] = res[3] = res[0];
                DEST(res, 1);
                break;
            }
            case PICA_MOVA: {
                fvec a;
                SRC1(a, 1);
                if (desc.destmask & BIT(3 - 0)) {
                    shu->a[0] = a[0];
                }
                if (desc.destmask & BIT(3 - 1)) {
                    shu->a[1] = a[1];
                }
                break;
            }
            case PICA_MOV: {
                fvec a;
                SRC1(a, 1);
                DEST(a, 1);
                break;
            }
            case PICA_NOP:
                break;
            case PICA_BREAK:
            case PICA_BREAKC: {
                bool cond;
                if (instr.opcode == PICA_BREAKC) {
                    cond = condop(instr.fmt2.op, shu->cmp[0], shu->cmp[1],
                                  instr.fmt2.refx, instr.fmt2.refy);
                } else {
                    cond = true;
                }
                if (cond) {
                    pc = TOP.pc + 1;
                    POP();
                }
                break;
            }
            case PICA_END:
                return;
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                bool cond;
                if (instr.opcode == PICA_CALLC) {
                    cond = condop(instr.fmt2.op, shu->cmp[0], shu->cmp[1],
                                  instr.fmt2.refx, instr.fmt2.refy);
                } else if (instr.opcode == PICA_CALLU) {
                    cond = shu->b & BIT(instr.fmt3.c);
                } else cond = true;
                if (cond) {
                    PUSH();
                    TOP.dest = pc;
                    TOP.loop = false;
                    pc = instr.fmt3.dest;
                    TOP.pc = pc + instr.fmt3.num;
                }
                break;
            }
            case PICA_IFU:
            case PICA_IFC: {
                bool cond;
                if (instr.opcode == PICA_IFU) {
                    cond = shu->b & BIT(instr.fmt3.c);
                } else {
                    cond = condop(instr.fmt2.op, shu->cmp[0], shu->cmp[1],
                                  instr.fmt2.refx, instr.fmt2.refy);
                }
                if (cond) {
                    PUSH();
                    TOP.pc = instr.fmt2.dest;
                    TOP.dest = instr.fmt2.dest + instr.fmt2.num;
                    TOP.loop = false;
                } else {
                    pc = instr.fmt2.dest;
                }
                break;
            }
            case PICA_LOOP: {
                shu->al = shu->i[instr.fmt3.c & 3][1];
                PUSH();
                TOP.pc = instr.fmt3.dest + 1;
                TOP.dest = pc;
                TOP.loop = true;
                TOP.idx = 0;
                TOP.inc = shu->i[instr.fmt3.c & 3][2];
                TOP.max = shu->i[instr.fmt3.c & 3][0];
                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                bool cond;
                if (instr.opcode == PICA_JMPC) {
                    cond = condop(instr.fmt2.op, shu->cmp[0], shu->cmp[1],
                                  instr.fmt2.refx, instr.fmt2.refy);
                } else {
                    cond = shu->b & BIT(instr.fmt3.c);
                    if (instr.fmt3.num & 1) cond = !cond;
                }
                if (cond) {
                    pc = instr.fmt3.dest;
                }
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                fvec a, b;
                SRC1(a, 1c);
                SRC2(b, 1c);
                shu->cmp[0] = compare(instr.fmt1c.cmpx, a[0], b[0]);
                shu->cmp[1] = compare(instr.fmt1c.cmpy, a[1], b[1]);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];
                fvec a, b, c;
                SRC1(a, 5);
                if (instr.fmt5.opcode & 1) {
                    SRC2(b, 5);
                    SRC3(c, 5);
                } else {
                    SRC2(b, 5i);
                    SRC3(c, 5i);
                }

                fvec res;
                res[0] = MUL(a[0], b[0]) + c[0];
                res[1] = MUL(a[1], b[1]) + c[1];
                res[2] = MUL(a[2], b[2]) + c[2];
                res[3] = MUL(a[3], b[3]) + c[3];
                DEST(res, 5);
                break;
            }
            default:
                lerror("unknown PICA instruction %08x (opcode %x)", instr.w,
                       instr.opcode);
                break;
        }

        while (TOP.pc == pc) {
            if (TOP.loop) {
                shu->al += TOP.inc;
                if (TOP.idx++ == TOP.max) {
                    POP();
                } else {
                    pc = TOP.dest;
                }
            } else {
                pc = TOP.dest;
                POP();
            }
        }
    }
}

void pica_shader_exec(ShaderUnit* shu) {
    shader_run(shu);
}

static char coordnames[4][2] = {"x", "y", "z", "w"};

void disasmsrc(u32 n, u8 idx, u8 swizzle, bool neg) {
    if (neg) printf("-");
    if (n < 0x10) printf("v%d", n);
    else if (n < 0x20) printf("r%d", n - 0x10);
    else {
        n -= 0x20;
        if (idx) {
            printf("c%d[", n);
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
            printf("]");
        } else printf("c%d", n);
    }
    if (swizzle != 0b00011011) {
        printf(".");
        u8 sw[4];
        for (int i = 0; i < 4; i++) {
            sw[i] = (swizzle >> 2 * (3 - i)) & 3;
        }
        u8 ct = 4;
        if (sw[2] == sw[3]) {
            ct--;
            if (sw[1] == sw[3]) {
                ct--;
                if (sw[0] == sw[3]) {
                    ct--;
                }
            }
        }
        for (int i = 0; i < ct; i++) {
            printf("%s", coordnames[sw[i]]);
        }
    }
}

void disasmdest(u32 n, u8 mask) {
    if (n < 0x10) printf("o%d", n);
    else printf("r%d", n - 0x10);
    if (mask != 0b1111) {
        printf(".");
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) printf("%s", coordnames[i]);
        }
    }
}

#define DSRC(i, _fmt)                                                          \
    disasmsrc(instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                     \
              desc.src##i##swizzle, desc.src##i##neg)
#define DSRC1(fmt) DSRC(1, fmt)
#define DSRC2(fmt) DSRC(2, fmt)
#define DSRC3(fmt) DSRC(3, fmt)
#define DDEST(_fmt) disasmdest(instr.fmt##_fmt.dest, desc.destmask)

void disasm_block(ShaderUnit* shu, u32 start, u32 num);

static inline void disasmcondop(u32 op, bool refx, bool refy) {
    switch (op) {
        case 0:
            printf("%s and %s", refx ? "x" : "not x", refy ? "y" : "not y");
            break;
        case 1:
            printf("%s or %s", refx ? "x" : "not x", refy ? "y" : "not y");
            break;
        case 2:
            printf("%s", refx ? "x" : "not x");
            break;
        case 3:
            printf("%s", refy ? "y" : "not y");
            break;
    }
}

static inline void disasmcompare(u32 op) {
    switch (op) {
        case 0:
            printf("eq");
            break;
        case 1:
            printf("ne");
            break;
        case 2:
            printf("lt");
            break;
        case 3:
            printf("le");
            break;
        case 4:
            printf("gt");
            break;
        case 5:
            printf("ge");
            break;
    }
}

#define DISASMFMT1(name)                                                       \
    printf(#name);                                                             \
    printf(" ");                                                               \
    DDEST(1);                                                                  \
    printf(", ");                                                              \
    DSRC1(1);                                                                  \
    printf(", ");                                                              \
    DSRC2(1);                                                                  \
    break;

#define DISASMFMT1U(name)                                                      \
    printf(#name);                                                             \
    printf(" ");                                                               \
    DDEST(1);                                                                  \
    printf(", ");                                                              \
    DSRC1(1);                                                                  \
    break;

#define DISASMFMT1I(name)                                                      \
    printf(#name);                                                             \
    printf(" ");                                                               \
    DDEST(1i);                                                                 \
    printf(", ");                                                              \
    DSRC1(1i);                                                                 \
    printf(", ");                                                              \
    DSRC2(1i);                                                                 \
    break;

static struct {
    Vector(PICAInstr) calls;
    u32 depth;
} disasm;

u32 disasm_instr(ShaderUnit* shu, u32 pc) {
    PICAInstr instr = shu->code[pc++];
    OpDesc desc = shu->opdescs[instr.desc];
    switch (instr.opcode) {
        case PICA_ADD:
            DISASMFMT1(add);
        case PICA_DP3:
            DISASMFMT1(dp3);
        case PICA_DP4:
            DISASMFMT1(dp4);
        case PICA_DST:
            DISASMFMT1(dst);
        case PICA_DSTI:
            DISASMFMT1I(dst);
        case PICA_EX2:
            DISASMFMT1U(ex2);
        case PICA_LG2:
            DISASMFMT1U(lg2);
        case PICA_MUL:
            DISASMFMT1(mul);
        case PICA_SGE:
            DISASMFMT1(sge);
        case PICA_SLT:
            DISASMFMT1(slt);
        case PICA_SGEI:
            DISASMFMT1I(sge);
        case PICA_SLTI:
            DISASMFMT1I(slt);
        case PICA_FLR:
            DISASMFMT1U(flr);
        case PICA_MAX:
            DISASMFMT1(max);
        case PICA_MIN:
            DISASMFMT1(min);
        case PICA_RCP:
            DISASMFMT1U(rcp);
        case PICA_RSQ:
            DISASMFMT1U(rsq);
        case PICA_MOVA:
            printf("mova a.");
            if (desc.destmask & BIT(3 - 0)) {
                printf("x");
            }
            if (desc.destmask & BIT(3 - 1)) {
                printf("y");
            }
            printf(", ");
            DSRC1(1);
            break;
        case PICA_MOV:
            DISASMFMT1U(mov);
        case PICA_NOP:
            printf("nop");
            break;
        case PICA_BREAK:
            printf("break");
            break;
        case PICA_BREAKC:
            printf("breakc");
            disasmcondop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
            break;
        case PICA_END:
            printf("end");
            pc = -1;
            break;
        case PICA_CALL:
        case PICA_CALLC:
        case PICA_CALLU: {
            if (instr.opcode == PICA_CALLU) {
                printf("callu b%d, ", instr.fmt3.c);
            } else if (instr.opcode == PICA_CALLC) {
                printf("callc ");
                disasmcondop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                printf(", ");
            } else {
                printf("call ");
            }
            printf("proc_%03x", instr.fmt2.dest);
            bool found = false;
            Vec_foreach(c, disasm.calls) {
                if (c->fmt2.dest == instr.fmt2.dest) {
                    found = true;
                    if (c->fmt2.num < instr.fmt2.num)
                        c->fmt2.num = instr.fmt2.num;
                }
            }
            if (!found) {
                Vec_push(disasm.calls, instr);
            }
            break;
        }
        case PICA_IFU:
        case PICA_IFC: {
            if (instr.opcode == PICA_IFU) {
                printf("ifu b%d", instr.fmt3.c);
            } else {
                printf("ifc ");
                disasmcondop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
            }
            printf("\n");
            disasm_block(shu, pc, instr.fmt2.dest - pc);
            if (instr.fmt2.num) {
                printf("%14s", "");
                for (int i = 0; i < disasm.depth; i++) {
                    printf("%4s", "");
                }
                printf("else\n");
                disasm_block(shu, instr.fmt2.dest, instr.fmt2.num);
            }
            printf("%14s", "");
            for (int i = 0; i < disasm.depth; i++) {
                printf("%4s", "");
            }
            printf("end if");
            pc = instr.fmt2.dest + instr.fmt2.num;
            break;
        }
        case PICA_LOOP: {
            printf("loop i%d\n", instr.fmt3.c);
            disasm_block(shu, pc, instr.fmt3.dest + 1 - pc);
            printf("%14s", "");
            for (int i = 0; i < disasm.depth; i++) {
                printf("%4s", "");
            }
            printf("end loop");
            pc = instr.fmt3.dest + 1;
            break;
        }
        case PICA_JMPC:
        case PICA_JMPU: {
            if (instr.opcode == PICA_JMPU) {
                printf("jmpu %s b%d, ", (instr.fmt2.num & 1) ? "not" : "",
                       instr.fmt3.c);
            } else {
                printf("jmpc ");
                disasmcondop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                printf(", ");
            }
            printf("%03x", instr.fmt2.dest);
            break;
        }
        case PICA_CMP ... PICA_CMP + 1: {
            printf("cmp ");
            printf("x ");
            disasmcompare(instr.fmt1c.cmpx);
            printf(", ");
            printf("y ");
            disasmcompare(instr.fmt1c.cmpy);
            printf(", ");
            DSRC1(1c);
            printf(", ");
            DSRC2(1c);
            break;
        }
        case PICA_MAD ... PICA_MAD + 0xf: {
            desc = shu->opdescs[instr.fmt5.desc];
            printf("mad ");
            DDEST(5);
            printf(", ");
            DSRC3(5);
            printf(", ");
            if (instr.fmt5.opcode & 1) {
                DSRC2(5);
                printf(", ");
                DSRC3(5);
            } else {
                DSRC2(5i);
                printf(", ");
                DSRC3(5i);
            }
            break;
        }
        default:
            printf("unknown");
            break;
    }
    return pc;
}

void disasm_block(ShaderUnit* shu, u32 start, u32 num) {
    disasm.depth++;
    u32 end = SHADER_CODE_SIZE;
    if (start + num < end) end = start + num;
    for (u32 pc = start; pc < end;) {
        printf("%03x: %08x ", pc, shu->code[pc].w);
        for (int i = 0; i < disasm.depth; i++) {
            printf("%4s", "");
        }
        pc = disasm_instr(shu, pc);
        printf("\n");
    }
    disasm.depth--;
}

void pica_shader_disasm(ShaderUnit* shu) {
    for (int i = 0; i < 8; i++) {
        printf("const fvec c%d = ", 95 - i);
        print_fvec(shu->c[95 - i]);
        printf("\n");
    }

    disasm.depth = 0;
    Vec_init(disasm.calls);
    printf("proc main\n");
    disasm_block(shu, shu->entrypoint, SHADER_CODE_SIZE);
    printf("end proc\n");
    for (int i = 0; i < disasm.calls.size; i++) {
        u32 start = disasm.calls.d[i].fmt3.dest;
        u32 num = disasm.calls.d[i].fmt3.num;
        printf("proc proc_%03x\n", start);
        disasm_block(shu, start, num);
        printf("end proc\n");
    }
    Vec_free(disasm.calls);
}