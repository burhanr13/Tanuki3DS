#include "shader.h"

#include <math.h>

#include "gpu.h"

void readsrc(GPU* gpu, fvec src, u32 n, u8 idx, u8 swizzle, bool neg) {
    fvec* rn;
    if (n < 0x10) rn = &gpu->in[n];
    else if (n < 0x20) rn = &gpu->reg[n - 0x10];
    else {
        n -= 0x20;
        if (idx) {
            switch (idx) {
                case 1:
                    n += gpu->addr[0];
                    break;
                case 2:
                    n += gpu->addr[1];
                    break;
                case 3:
                    n += gpu->loopct;
                    break;
            }
            n &= 0x7f;
            if (n < 0x60) {
                rn = &gpu->cst[n];
            } else {
                static fvec dummy = {1, 1, 1, 1};
                rn = &dummy;
            }
        } else rn = &gpu->cst[n];
    }

    for (int i = 0; i < 4; i++) {
        src[i] = (*rn)[(swizzle >> 2 * (3 - i)) & 3];
        if (neg) src[i] = -src[i];
    }
}

void writedest(GPU* gpu, fvec dest, u32 n, u8 mask) {
    fvec* rn;
    if (n < 0x10) rn = &gpu->out[n];
    else rn = &gpu->reg[n - 0x10];
    for (int i = 0; i < 4; i++) {
        if (mask & BIT(3 - i)) (*rn)[i] = dest[i];
    }
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(gpu, v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,               \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(v, fmt) SRC(v, 1, fmt)
#define SRC2(v, fmt) SRC(v, 2, fmt)
#define SRC3(v, fmt) SRC(v, 3, fmt)
#define DEST(v, _fmt) writedest(gpu, v, instr.fmt##_fmt.dest, desc.destmask)

void exec_block(GPU* gpu, u32 start, u32 num);

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

u32 exec_instr(GPU* gpu, u32 pc, bool* end) {
    PICAInstr instr = gpu->code[pc++];
    OpDesc desc = {gpu->opdescs[instr.desc]};
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
        case PICA_SGE: {
            fvec a, b;
            SRC1(a, 1);
            SRC2(b, 1);
            fvec res;
            res[0] = a[0] >= b[0];
            res[1] = a[1] >= b[1];
            res[2] = a[2] >= b[2];
            res[3] = a[3] >= b[3];
            DEST(res, 1);
            break;
        }
        case PICA_SLT: {
            fvec a, b;
            SRC1(a, 1);
            SRC2(b, 1);
            fvec res;
            res[0] = a[0] < b[0];
            res[1] = a[1] < b[1];
            res[2] = a[2] < b[2];
            res[3] = a[3] < b[3];
            DEST(res, 1);
            break;
        }
        case PICA_MAX: {
            fvec a, b;
            SRC1(a, 1);
            SRC2(b, 1);
            fvec res;
            res[0] = fmaxf(a[0], b[0]);
            res[1] = fmaxf(a[1], b[1]);
            res[2] = fmaxf(a[2], b[2]);
            res[3] = fmaxf(a[3], b[3]);
            DEST(res, 1);
            break;
        }
        case PICA_MIN: {
            fvec a, b;
            SRC1(a, 1);
            SRC2(b, 1);
            fvec res;
            res[0] = fminf(a[0], b[0]);
            res[1] = fminf(a[1], b[1]);
            res[2] = fminf(a[2], b[2]);
            res[3] = fminf(a[3], b[3]);
            DEST(res, 1);
            break;
        }
        case PICA_RCP: {
            fvec a;
            SRC1(a, 1);
            fvec res;
            res[0] = 1 / a[0];
            res[1] = res[2] = res[3] = res[0];
            break;
        }
        case PICA_RSQ: {
            fvec a;
            SRC1(a, 1);
            fvec res;
            res[0] = 1 / sqrtf(a[0]);
            res[1] = res[2] = res[3] = res[0];
            break;
        }
        case PICA_MOVA: {
            fvec a;
            SRC1(a, 1);
            if (desc.destmask & BIT(3 - 0)) {
                gpu->addr[0] = a[0];
            }
            if (desc.destmask & BIT(3 - 1)) {
                gpu->addr[1] = a[1];
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
                cond = condop(instr.fmt2.op, gpu->cmp[0], gpu->cmp[1],
                              instr.fmt2.refx, instr.fmt2.refy);
            } else {
                cond = true;
            }
            if (cond) pc = -1;
            break;
        }
        case PICA_END:
            *end = true;
            pc = -1;
            break;
        case PICA_CALL:
        case PICA_CALLC:
        case PICA_CALLU: {
            bool cond;
            if (instr.opcode == PICA_CALLC) {
                cond = condop(instr.fmt2.op, gpu->cmp[0], gpu->cmp[1],
                              instr.fmt2.refx, instr.fmt2.refy);
            } else if (instr.opcode == PICA_CALLU) {
                cond = gpu->io.VSH.booluniform & BIT(instr.fmt3.c);
            } else cond = true;
            if (cond) {
                exec_block(gpu, instr.fmt3.dest, instr.fmt3.num);
            }
            break;
        }
        case PICA_IFU:
        case PICA_IFC: {
            bool cond;
            if (instr.opcode == PICA_IFU) {
                cond = gpu->io.VSH.booluniform & BIT(instr.fmt3.c);
            } else {
                cond = condop(instr.fmt2.op, gpu->cmp[0], gpu->cmp[1],
                              instr.fmt2.refx, instr.fmt2.refy);
            }
            if (cond) {
                exec_block(gpu, pc, instr.fmt2.dest - pc);
            } else {
                exec_block(gpu, instr.fmt2.dest, instr.fmt2.num);
            }
            pc = instr.fmt2.dest + instr.fmt2.num;
            break;
        }
        case PICA_LOOP: {
            gpu->loopct = gpu->io.VSH.intuniform[instr.fmt3.c].y;
            for (int i = 0; i <= gpu->io.VSH.intuniform[instr.fmt3.c].x; i++) {
                exec_block(gpu, pc, instr.fmt3.dest + 1 - pc);
                gpu->loopct += gpu->io.VSH.intuniform[instr.fmt3.c].z;
            }
            pc = instr.fmt3.dest + 1;
            break;
        }
        case PICA_JMPC:
        case PICA_JMPU: {
            bool cond;
            if (instr.opcode == PICA_CALLC) {
                cond = condop(instr.fmt2.op, gpu->cmp[0], gpu->cmp[1],
                              instr.fmt2.refx, instr.fmt2.refy);
            } else {
                cond = gpu->io.VSH.booluniform & BIT(instr.fmt3.c);
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
            gpu->cmp[0] = compare(instr.fmt1c.cmpx, a[0], b[0]);
            gpu->cmp[1] = compare(instr.fmt1c.cmpy, a[1], b[1]);
            break;
        }
        case PICA_MAD ... PICA_MAD + 0xf: {
            desc.w = gpu->opdescs[instr.fmt5.desc];
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
            lerror("unknown PICA instruction %08x", instr.w);
            break;
    }
    return pc;
}

void exec_block(GPU* gpu, u32 start, u32 num) {
    bool shader_end = false;
    u32 end = BIT(12);
    if (start + num < end) end = start + num;
    for (u32 pc = start; !shader_end && pc < end;
         pc = exec_instr(gpu, pc, &shader_end));
}

void exec_vshader(GPU* gpu) {
    exec_block(gpu, gpu->io.VSH.entrypoint, BIT(12));
}

static char coordnames[4][2] = {"x", "y", "z", "w"};

void disasmsrc(u32 n, u8 idx, u8 swizzle, bool neg) {
    if (neg) printf("-");
    if (n < 0x10) printf("v%d", n);
    else if (n < 0x20) printf("r%d", n - 0x10);
    else {
        n -= 0x20;
        if (idx) {
            printf("c(%d,", n);
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
            printf(")");
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

void disasm_block(GPU* gpu, u32 start, u32 num);

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

u32 disasm_instr(GPU* gpu, u32 pc) {
    PICAInstr instr = gpu->code[pc++];
    OpDesc desc = {gpu->opdescs[instr.desc]};
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
        case PICA_MUL:
            DISASMFMT1(mul);
        case PICA_SGE:
            DISASMFMT1(sge);
        case PICA_SLT:
            DISASMFMT1(slt);
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
            disasm_block(gpu, pc, instr.fmt2.dest - pc);
            if (instr.fmt2.num) {
                printf("%14s", "");
                for (int i = 0; i < disasm.depth; i++) {
                    printf("%4s", "");
                }
                printf("else\n");
                disasm_block(gpu, instr.fmt2.dest, instr.fmt2.num);
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
            disasm_block(gpu, pc, instr.fmt3.dest + 1 - pc);
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
            desc.w = gpu->opdescs[instr.fmt5.desc];
            printf("mad ");
            DDEST(5);
            printf(", ");
            if (instr.fmt5.opcode & 1) {
                DSRC1(5);
                printf(", ");
                DSRC2(5);
                printf(", ");
            } else {
                DSRC1(5i);
                printf(", ");
                DSRC2(5i);
                printf(", ");
            }
            DSRC3(5);
            break;
        }
        default:
            printf("unknown");
            break;
    }
    return pc;
}

void disasm_block(GPU* gpu, u32 start, u32 num) {
    disasm.depth++;
    u32 end = BIT(12);
    if (start + num < end) end = start + num;
    for (u32 pc = start; pc < end;) {
        printf("%03x: %08x ", pc, gpu->code[pc].w);
        for (int i = 0; i < disasm.depth; i++) {
            printf("%4s", "");
        }
        pc = disasm_instr(gpu, pc);
        printf("\n");
    }
    disasm.depth--;
}

void disasm_vshader(GPU* gpu) {
    for (int i = 0; i < 8; i++) {
        printf("const fvec c%d = ", 95 - i);
        PRINTFVEC(gpu->cst[95 - i]);
        printf("\n");
    }

    disasm.depth = 0;
    Vec_init(disasm.calls);
    printf("proc main\n");
    disasm_block(gpu, gpu->io.VSH.entrypoint, BIT(12));
    printf("end proc\n");
    Vec_foreach(c, disasm.calls) {
        u32 start = c->fmt3.dest;
        u32 num = c->fmt3.num;
        printf("proc proc_%03x\n", start);
        disasm_block(gpu, start, num);
        printf("end proc\n");
    }
    Vec_free(disasm.calls);
}