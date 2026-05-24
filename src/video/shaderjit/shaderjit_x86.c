#ifdef __x86_64__

#include "shaderjit_x86.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif
#include <math.h>

#define RAS_CTX_VAR this->code
#include <ras/ras_x64.h>

// #define JIT_DISASM

#define reg_v R10
#define reg_o R11
#define reg_r RBP
#define reg_c RCX
#define reg_i RDX
#define reg_b RSI
#define reg_ax R8
#define reg_ay R9
#define reg_al RDI
#define reg_cmpx RAX
#define reg_cmpy AH
#define reg_cmp RAX
#define negmask XMM4
#define ones XMM5
#define tmp XMM3
#define loopcounter R12

X86ShaderJitBackend* shaderjit_x86_init() {
    return calloc(1, sizeof(X86ShaderJitBackend));
}

static void readsrc(X86ShaderJitBackend* this, rasX64Xmm dst, u32 n, u8 idx,
                    u8 swizzle, bool neg) {
    rasX64Mem addr;
    if (n < 0x10) {
        addr = PTR(16 * n, reg_v);
    } else if (n < 0x20) {
        n -= 0x10;
        addr = PTR(16 * n, reg_r);
    } else {
        n -= 0x20;
        if (idx == 0) {
            addr = PTR(16 * n, reg_c);
        } else {
            switch (idx) {
                case 1:
                    MOVSXBQ(RBX, reg_ax);
                    break;
                case 2:
                    MOVSXBQ(RBX, reg_ay);
                    break;
                case 3:
                    MOVZXBQ(RBX, reg_al);
                    break;
            }
            ADDQ(RBX, n);
            ANDQ(RBX, 0x7f);
            SHLQ(RBX, 4);
            addr = PTR(reg_c, RBX);
        }
    }

    if (swizzle != 0b00011011) {
        // pica swizzles are backwards
        swizzle = (swizzle & 0xcc) >> 2 | (swizzle & 0x33) << 2;
        swizzle = (swizzle & 0xf0) >> 4 | (swizzle & 0x0f) << 4;
        PSHUFD(dst, addr, swizzle);
    } else {
        MOVAPS(dst, addr);
    }
    if (neg) {
        XORPS(dst, negmask);
    }
}

static void writedest(X86ShaderJitBackend* this, rasX64Xmm src, int n,
                      u8 mask) {
    rasX64Mem addr;
    if (n < 0x10) {
        addr = PTR(16 * n, reg_o);
    } else {
        n -= 0x10;
        addr = PTR(16 * n, reg_r);
    }
    if (mask != 0b1111) {
        // pica destination masks are also backwards
        mask = (mask & 0b1010) >> 1 | (mask & 0b0101) << 1;
        mask = (mask & 0b1100) >> 2 | (mask & 0b0011) << 2;
        // invert the mask since we blend the opposite direction
        mask ^= 0xf;
        BLENDPS(src, addr, mask);
    }
    MOVAPS(addr, src);
}

static void compare(X86ShaderJitBackend* this, rasX64Reg dst, u8 op) {
    switch (op) {
        case 0:
            SETE(dst);
            break;
        case 1:
            SETNE(dst);
            break;
        case 2:
            SETB(dst);
            break;
        case 3:
            SETBE(dst);
            break;
        case 4:
            SETA(dst);
            break;
        case 5:
            SETAE(dst);
            break;
        default:
            MOVB(dst, 1);
            break;
    }
}

// z flag clear if condition is true
static void condop(X86ShaderJitBackend* this, u32 op, bool refx, bool refy) {
    MOVW(RBX, reg_cmp);
    switch (op) {
        case 0:
            if (!refx) XORB(RBX, 1);
            if (!refy) XORB(BH, 1);
            ORB(RBX, BH);
            return;
        case 1:
            if (!refx) XORB(RBX, 1);
            if (!refy) XORB(BH, 1);
            ANDB(RBX, BH);
            return;
        case 2:
            if (refx) {
                ANDB(RBX, 1);
            } else {
                XORB(RBX, 1);
            }
            return;
        case 3:
            if (refx) {
                ANDB(BH, 1);
            } else {
                XORB(BH, 1);
            }
            return;
    }
}

// 0 * anything is 0 (ieee noncompliant)
// this is important to emulate
// zeros any lanes of XMM1 where XMM0 was 0
static void setupMul(X86ShaderJitBackend* this) {
    XORPS(tmp, tmp);
    CMPNEQPS(tmp, XMM0);
    ANDPS(XMM1, tmp);
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(this, v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,              \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(v, fmt) SRC(v, 1, fmt)
#define SRC2(v, fmt) SRC(v, 2, fmt)
#define SRC3(v, fmt) SRC(v, 3, fmt)
#define DEST(v, _fmt) writedest(this, v, instr.fmt##_fmt.dest, desc.destmask)


static void compileBlock(X86ShaderJitBackend* this, ShaderUnit* shu, u32 start,
                         u32 len) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
    u32 farthestjmp = 0;
    while (pc < end) {
        L(this->jmplabels.d[pc]);

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                ADDPS(XMM0, XMM1);
                DEST(XMM0, 1);
                break;
            }
            case PICA_DP3: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                setupMul(this);
                DPPS(XMM0, XMM1, 0x7f);
                DEST(XMM0, 1);
                break;
            }
            case PICA_DP4: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                setupMul(this);
                DPPS(XMM0, XMM1, 0xff);
                DEST(XMM0, 1);
                break;
            }
            case PICA_DPH:
            case PICA_DPHI: {
                if (instr.opcode == PICA_DPH) {
                    SRC1(XMM0, 1);
                    SRC2(XMM1, 1);
                } else {
                    SRC1(XMM0, 1i);
                    SRC2(XMM1, 1i);
                }
                INSERTPS(XMM0, ones, 0 << 6 | 3 << 4); // src1[3] = 1
                setupMul(this);
                DPPS(XMM0, XMM1, 0xff);
                DEST(XMM0, 1);
                break;
            }
            case PICA_EX2: {
                this->usingEx2 = true;
                SRC1(XMM0, 1);
                CALL(this->ex2func);
                SHUFPS(XMM0, XMM0, 0);
                DEST(XMM0, 1);
                break;
            }
            case PICA_LG2: {
                this->usingLg2 = true;
                SRC1(XMM0, 1);
                CALL(this->lg2func);
                SHUFPS(XMM0, XMM0, 0);
                DEST(XMM0, 1);
                break;
            }
            case PICA_MUL: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                setupMul(this);
                MULPS(XMM0, XMM1);
                DEST(XMM0, 1);
                break;
            }
            case PICA_FLR: {
                SRC1(XMM0, 1);
                ROUNDPS(XMM0, XMM0, 1); // round towards -inf
                DEST(XMM0, 1);
                break;
            }
            case PICA_MIN: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                MINPS(XMM0, XMM1);
                DEST(XMM0, 1);
                break;
            }
            case PICA_MAX: {
                SRC1(XMM0, 1);
                SRC2(XMM1, 1);
                MAXPS(XMM0, XMM1);
                DEST(XMM0, 1);
                break;
            }
            case PICA_RCP: {
                SRC1(XMM0, 1);
                RCPSS(XMM0, XMM0);
                SHUFPS(XMM0, XMM0, 0);
                DEST(XMM0, 1);
                break;
            }
            case PICA_RSQ: {
                SRC1(XMM0, 1);
                RSQRTSS(XMM0, XMM0);
                SHUFPS(XMM0, XMM0, 0);
                DEST(XMM0, 1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(XMM0, 1);
                    SRC2(XMM1, 1);
                } else {
                    SRC1(XMM0, 1i);
                    SRC2(XMM1, 1i);
                }
                CMPNLTPS(XMM0, XMM1);
                ANDPS(XMM0, ones);
                DEST(XMM0, 1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(XMM0, 1);
                    SRC2(XMM1, 1);
                } else {
                    SRC1(XMM0, 1i);
                    SRC2(XMM1, 1i);
                }
                CMPLTPS(XMM0, XMM1);
                ANDPS(XMM0, ones);
                DEST(XMM0, 1);
                break;
            }
            case PICA_MOVA: {
                SRC1(XMM0, 1);
                CVTTPS2DQ(XMM0, XMM0);
                if (desc.destmask & BIT(3 - 0)) {
                    MOVD(reg_ax, XMM0);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    PSRLDQ(XMM0, 4);
                    MOVD(reg_ay, XMM0);
                }
                break;
            }
            case PICA_MOV: {
                SRC1(XMM0, 1);
                DEST(XMM0, 1);
                break;
            }
            case PICA_NOP:
                break;
            case PICA_END:
                // there can be multiple end instructions
                // in the same main procedure, if the first one
                // is skipped with a jump
                // if this is not the final end, we just jump to the end label
                if (farthestjmp < pc) return;
                else {
                    JMP(this->curEndLab, NEAR);
                    break;
                }
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                LABEL(lelse);
                if (instr.opcode == PICA_CALLU) {
                    TESTW(reg_b, BIT(instr.fmt3.c));
                } else if (instr.opcode == PICA_CALLC) {
                    condop(this, instr.fmt2.op, instr.fmt2.refx,
                           instr.fmt2.refy);
                }
                if (instr.opcode != PICA_CALL) JZ(lelse);
                CALL(this->jmplabels.d[instr.fmt2.dest]);
                L(lelse);

                bool found = false;
                Vec_foreach(call, this->calls) {
                    if (call->fmt2.dest == instr.fmt2.dest) {
                        found = true;
                        if (call->fmt2.num != instr.fmt2.num)
                            lerror("calling same function with different size");
                    }
                }
                if (!found) {
                    Vec_push(this->calls, instr);
                }
                break;
            }
            case PICA_IFU:
            case PICA_IFC: {
                LABEL(lelse);
                LABEL(lendif);

                if (instr.opcode == PICA_IFU) {
                    TESTW(reg_b, BIT(instr.fmt3.c));
                } else {
                    condop(this, instr.fmt2.op, instr.fmt2.refx,
                           instr.fmt2.refy);
                }
                JZ(lelse, NEAR);

                compileBlock(this, shu, pc, instr.fmt2.dest - pc);
                if (instr.fmt2.num) {
                    JMP(lendif, NEAR);
                    L(lelse);
                    compileBlock(this, shu, instr.fmt2.dest, instr.fmt2.num);
                    L(lendif);
                } else {
                    L(lelse);
                }

                pc = instr.fmt2.dest + instr.fmt2.num;
                break;
            }
            case PICA_LOOP: {
                LABEL(lloop);

                PUSH(loopcounter);
                MOVB(loopcounter, 0);
                MOVB(reg_al, PTR(4 * (instr.fmt3.c & 3) + 1, reg_i));
                L(lloop);

                compileBlock(this, shu, pc, instr.fmt3.dest + 1 - pc);

                ADDB(reg_al, PTR(4 * (instr.fmt3.c & 3) + 2, reg_i));
                INCB(loopcounter);
                CMPB(loopcounter, PTR(4 * (instr.fmt3.c & 3) + 0, reg_i));
                JBE(lloop, NEAR);

                POP(loopcounter);

                pc = instr.fmt3.dest + 1;

                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                if (instr.opcode == PICA_JMPU) {
                    TESTW(reg_b, BIT(instr.fmt3.c));
                    if (instr.fmt3.num & 1) {
                        JZ(this->jmplabels.d[instr.fmt3.dest], NEAR);
                    } else {
                        JNZ(this->jmplabels.d[instr.fmt3.dest], NEAR);
                    }
                } else {
                    condop(this, instr.fmt2.op, instr.fmt2.refx,
                           instr.fmt2.refy);
                    JNZ(this->jmplabels.d[instr.fmt3.dest], NEAR);
                }
                if (instr.fmt3.dest > farthestjmp)
                    farthestjmp = instr.fmt3.dest;
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                SRC1(XMM0, 1c);
                SRC2(XMM1, 1c);
                COMISS(XMM0, XMM1);
                compare(this, reg_cmpx, instr.fmt1c.cmpx);
                PSRLDQ(XMM0, 4);
                PSRLDQ(XMM1, 4);
                COMISS(XMM0, XMM1);
                compare(this, reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                SRC1(XMM0, 5);
                if (instr.fmt5.opcode & 1) {
                    SRC2(XMM1, 5);
                    SRC3(XMM2, 5);
                } else {
                    SRC2(XMM1, 5i);
                    SRC3(XMM2, 5i);
                }

                setupMul(this);
                MULPS(XMM0, XMM1);
                ADDPS(XMM0, XMM2);

                DEST(XMM0, 5);
                break;
            }
            default:
                lerror("unknown pica instr for JIT: %x (opcode %x)", instr.w,
                       instr.opcode);
        }
    }
}

static void compileEx2(X86ShaderJitBackend* this) {
    // takes x in XMM0 and return in XMM0

    LABEL(end);

    // constants for getting good input to polynomials (found through testing)
    const float exthr = 0.535f;

    L(this->ex2func);
    // check nan
    COMISS(XMM0, XMM0);
    JNE(end, NEAR);

    // we need registers
    PUSH(RAX);

    // x = n + r where n in Z, r in [0,1)
    // 2^x = 2^n * 2^r, 2^r will be in [1,2)
    // so it ends up just being a float
    ROUNDSS(XMM1, XMM0, 1);
    CVTSS2SIQ(RBX, XMM1);
    SUBSS(XMM0, XMM1);
    // now n in rbx and r in xmm0

    // translate from [0, 1) -> [exthr-1, exthr)
    MOVD(RAX, F2I(exthr));
    MOVD(XMM1, RAX);
    COMISS(XMM0, XMM1);
    LABEL(l1);
    JB(l1);
    SUBSS(XMM0, ones);
    L(l1);

    // make n into float exponent
    ADDQ(RBX, 127);
    XORD(RAX, RAX);
    CMPQ(RBX, RAX);
    CMOVLQ(RBX, RAX);
    MOVD(RAX, 0xff);
    CMPQ(RBX, RAX);
    CMOVGD(RBX, RAX);
    SHLD(RBX, 23);

    // 2^r = e^(r * ln2)
    // e^x ~= ((1/6 * x + 1/2) * x + 1) * x + 1
    const float c0 = M_LN2 * M_LN2 * M_LN2 / 6;
    const float c1 = M_LN2 * M_LN2 / 2;
    const float c2 = M_LN2;
    // c3 is just 1
    // now 2^x ~= ((c0 * x + c1) * x + c2) * x + c3

    MOVD(RAX, F2I(c0));
    MOVD(XMM1, RAX);
    MOVD(RAX, F2I(c1));
    MOVD(XMM2, RAX);
    MOVD(RAX, F2I(c2));
    MOVD(XMM3, RAX);

    MULSS(XMM1, XMM0);
    ADDSS(XMM1, XMM2);
    MULSS(XMM1, XMM0);
    ADDSS(XMM1, XMM3);
    MULSS(XMM1, XMM0);
    ADDSS(XMM1, ones);

    // extract the mantissa and insert into the result
    MOVD(RAX, XMM1);
    ANDD(RAX, MASK(23));
    ORD(RBX, RAX);
    MOVD(XMM0, RBX);

    POP(RAX);

    L(end);
    RET();
}

static void compileLg2(X86ShaderJitBackend* this) {
    // takes x in XMM0 and return in XMM0

    // constants for getting good input to polynomials (found through testing)
    const float lgthr = 1.35f;

    LABEL(lnan);
    LABEL(lminf);

    L(this->lg2func);
    // check nan and 0
    COMISS(XMM0, XMM0);
    JNE(lnan, NEAR);
    XORPS(XMM1, XMM1);
    COMISS(XMM0, XMM1);
    JE(lminf, NEAR);
    JB(lnan, NEAR);

    PUSH(RAX);

    // x = 2^n * r where n in Z and r in [1,2)
    // log2(x) = n + log2(r)

    MOVD(RAX, XMM0);
    MOVD(RBX, RAX);
    SHRD(RBX, 23);
    ANDD(RAX, MASK(23));
    ORD(RAX, 0x3f80'0000);
    MOVD(XMM0, RAX);
    // now n in ebx and r in xmm0

    // translate from [1, 2) -> [lgthr/2, lgthr)
    MOVD(RAX, F2I(lgthr));
    MOVD(XMM1, RAX);
    COMISS(XMM0, XMM1);
    LABEL(l1);
    JB(l1);
    MOVD(RAX, F2I(0.5f));
    MOVD(XMM1, RAX);
    MULSS(XMM0, XMM1);
    INCD(RBX);
    L(l1);

    // log2(r) = ln(r)/ln2
    // log polynomial is for x-1
    SUBSS(XMM0, ones);

    // ln(x+1) ~= (((-1/4 * x + 1/3) * x - 1/2) * x + 1) * x
    const float c0 = -1 / (4 * M_LN2);
    const float c1 = 1 / (3 * M_LN2);
    const float c2 = -1 / (2 * M_LN2);
    const float c3 = 1 / M_LN2;
    // log2(x+1) ~= (((c0 * x + c1) * x + c2) * x + c3) * x

    MOVD(RAX, F2I(c0));
    MOVD(XMM1, RAX);
    MOVD(RAX, F2I(c1));
    MOVD(XMM2, RAX);

    MULSS(XMM1, XMM0);
    ADDSS(XMM1, XMM2);

    MOVD(RAX, F2I(c2));
    MOVD(XMM2, RAX);
    MOVD(RAX, F2I(c3));
    MOVD(XMM3, RAX);

    MULSS(XMM1, XMM0);
    ADDSS(XMM1, XMM2);
    MULSS(XMM1, XMM0);
    ADDSS(XMM1, XMM3);
    MULSS(XMM1, XMM0);

    // res is n + log r
    CVTSI2SSD(XMM0, RBX);
    ADDSS(XMM0, XMM1);

    POP(RAX);

    RET();

    // degenerate cases
    L(lnan);
    MOVD(RAX, F2I(NAN));
    MOVD(XMM0, RAX);
    RET();
    L(lminf);
    MOVD(RAX, F2I(-INFINITY));
    MOVD(XMM0, RAX);
    RET();
}

// returns the label of the function for the given entrypoint
static rasLabel compileWithEntry(X86ShaderJitBackend* this, ShaderUnit* shu,
                                 u32 entry) {
    // this is so we only compile any functions that were not already compiled
    // while compiling previous entry points
    u32 callsStart = this->calls.size;

    this->curEndLab = LNEW();

#ifdef _WIN32
    auto arg = RCX;
#else
    auto arg = RDI;
#endif

    LABEL(entrylab);
    L(entrylab);
    PUSH(RBP);
    PUSH(RBX);
    PUSH(R12);
#ifdef _WIN32
    PUSH(RSI);
    PUSH(RDI);
#endif
    SUBQ(RSP, 16 * 16);
    MOVQ(reg_r, RSP);
    LEAQ(reg_v, PTR(offsetof(ShaderUnit, v), arg));
    LEAQ(reg_o, PTR(offsetof(ShaderUnit, o), arg));
    MOVQ(reg_i, PTR(offsetof(ShaderUnit, i), arg));
    MOVW(reg_b, PTR(offsetof(ShaderUnit, b), arg));
    MOVQ(reg_c, PTR(offsetof(ShaderUnit, c), arg));
    MOVD(RAX, BIT(31));
    MOVD(negmask, RAX);
    SHUFPS(negmask, negmask, 0);
    MOVD(RAX, 0x3f800000); // 1.0f
    MOVD(ones, RAX);
    SHUFPS(ones, ones, 0);

    compileBlock(this, shu, entry, SHADER_CODE_SIZE);

    L(this->curEndLab);
    ADDQ(RSP, 16 * 16);
#ifdef _WIN32
    POP(RDI);
    POP(RSI);
#endif
    POP(R12);
    POP(RBX);
    POP(RBP);
    RET();

    for (size_t i = callsStart; i < this->calls.size; i++) {
        compileBlock(this, shu, this->calls.d[i].fmt2.dest, this->calls.d[i].fmt2.num);
        RET();
    }

    return entrylab;
}

// it is possible to have multiple entrypoints in the same shader
// we keep track of them and whenever there is a new one we recompile
// the entire shader
ShaderJitFunc shaderjit_x86_get_code(X86ShaderJitBackend* this,
                                     ShaderUnit* shu) {
    Vec_foreach(e, this->entrypoints) {
        if (e->pc == shu->entrypoint)
            return rasGetLabelAddr(this->code, e->lab);
    }
    // couldn't find this entrypoint, so recompile
    X86ShaderEntrypoint e = {shu->entrypoint, nullptr};
    int i = Vec_push(this->entrypoints, e);

    if (this->code) rasDestroy(this->code);
    this->code = rasCreate(16384, 0);

    Vec_resize(this->jmplabels, SHADER_CODE_SIZE);
    for (int i = 0; i < SHADER_CODE_SIZE; i++) {
        this->jmplabels.d[i] = rasDeclareLabel(this->code);
    }
    this->ex2func = rasDeclareLabel(this->code);
    this->usingEx2 = false;
    this->lg2func = rasDeclareLabel(this->code);
    this->usingLg2 = false;

    Vec_foreach(e, this->entrypoints) {
        e->lab = compileWithEntry(this, shu, e->pc);
    }

    if (this->usingEx2) compileEx2(this);
    if (this->usingLg2) compileLg2(this);

    Vec_free(this->jmplabels);
    Vec_free(this->calls);

    rasReady(this->code);

#ifdef JIT_DISASM
    pica_shader_disasm(shu);
    shaderjit_arm_disassemble((void*) this);
#endif

    return rasGetLabelAddr(this->code, this->entrypoints.d[i].lab);
}

void shaderjit_x86_free(X86ShaderJitBackend* this) {
    if (!this) return;
    Vec_free(this->jmplabels);
    Vec_free(this->calls);
    Vec_free(this->entrypoints);
    rasDestroy(this->code);
    free(this);
}

#ifndef NOCAPSTONE
void shaderjit_x86_disassemble(X86ShaderJitBackend* this) {
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    size_t count = cs_disasm(handle, rasGetCode(this->code),
                             rasGetSize(this->code), 0, 0, &insn);
    printf("--------- Shader JIT Disassembly at %p ------------\n",
           rasGetCode(this->code));
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
#endif

#endif
