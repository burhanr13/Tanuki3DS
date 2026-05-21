#ifdef __aarch64__

#include "shaderjit_arm.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif
#include <math.h>

#define RAS_CTX_VAR this->code
#include <ras/ras_a64.h>

// #define JIT_DISASM

#define reg_v R0
#define reg_o R1
#define reg_r 16 // V16-V31
#define reg_c R2
#define reg_i R3
#define reg_b R4
#define reg_ax R5
#define reg_ay R6
#define reg_al R7
#define reg_cmpx R8
#define reg_cmpy R9
#define loopcount R10
// R11-R17 : temp
// V0-V7 : temp

ArmShaderJitBackend* shaderjit_arm_init() {
    return calloc(1, sizeof(ArmShaderJitBackend));
    // the ras code is initialized each time we have to recompile
    // for a new entrypoint
}

static rasA64VReg readsrc(ArmShaderJitBackend* this, rasA64VReg dst, u32 n, u8 idx,
                       u8 swizzle, bool neg) {
    rasA64VReg src = V3;
    if (swizzle == 0b00'01'10'11) {
        src = dst;
    }
    if (n < 0x10) {
        LDRQ(src, (reg_v, 16 * n));
    } else if (n < 0x20) {
        n -= 0x10;
        src = V(reg_r + n);
    } else {
        n -= 0x20;
        if (idx == 0) {
            LDRQ(src, (reg_c, 16 * n));
        } else {
            MOVW(R11, n);
            switch (idx) {
                case 1:
                    ADDW(R11, R11, reg_ax, SXTB());
                    break;
                case 2:
                    ADDW(R11, R11, reg_ay, SXTB());
                    break;
                case 3:
                    ADDW(R11, R11, reg_al, UXTB());
                    break;
            }
            ANDW(R11, R11, 0x7f);
            LDRQ(src, (reg_c, R11, UXTW(4)));
        }
    }

    if (swizzle != 0b00'01'10'11) {
        // no shuffle instruction on arm :(
        int swizzleidx[4];
        int idxcount[4] = {};
        for (int i = 0; i < 4; i++) {
            swizzleidx[3 - i] = swizzle & 3;
            idxcount[swizzle & 3]++;
            swizzle >>= 2;
        }
        // use DUP to transfer the most repeated
        // value
        // manually move each other one
        int maxidx;
        int maxcount = 0;
        for (int i = 0; i < 4; i++) {
            if (idxcount[i] > maxcount) {
                maxcount = idxcount[i];
                maxidx = i;
            }
        }
        if (maxcount > 1) {
            DUP4S(dst, src, maxidx);
        } else {
            maxidx = -1;
        }
        for (int i = 0; i < 4; i++) {
            if (swizzleidx[i] != maxidx) {
                MOVS(dst, i, src, swizzleidx[i]);
            }
        }
        if (neg) {
            FNEG4S(dst, dst);
        }
        return dst;
    } else {
        if (neg) {
            FNEG4S(dst, src);
            return dst;
        } else {
            return src;
        }
    }
}

// dest is either V0 or V16-V31
static rasA64VReg getdest(int n, u8 mask) {
    if (n >= 0x10 && mask == 0b1111) return V(reg_r + n - 0x10);
    else return V0;
}

// the result will be in V0
static void writedest(ArmShaderJitBackend* this, int n, u8 mask) {
    if (mask == 0b1111) {
        if (n < 0x10) {
            STRQ(V0, (reg_o, 16 * n));
        } else {
            // already handled by getdest
        }
    } else {
        // no blend on arm either :(
        if (n < 0x10) {
            // optimize storing single element mask to memory
            switch (mask) {
                case BIT(3 - 0):
                    STRS(V0, (reg_o, 16 * n + 0));
                    return;
                case BIT(3 - 1):
                    MOVS(V0, 0, V0, 1);
                    STRS(V0, (reg_o, 16 * n + 4));
                    return;
                case BIT(3 - 2):
                    MOVS(V0, 0, V0, 2);
                    STRS(V0, (reg_o, 16 * n + 8));
                    return;
                case BIT(3 - 3):
                    MOVS(V0, 0, V0, 3);
                    STRS(V0, (reg_o, 16 * n + 12));
                    return;
            }
        }
        rasA64VReg dst = V3;
        if (n < 0x10) {
            LDRQ(V3, (reg_o, 16 * n));
        } else {
            n -= 0x10;
            dst = V(reg_r + n);
        }
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) {
                MOVS(dst, i, V0, i);
            }
        }
        if (dst.idx == 3) STRQ(V3, (reg_o, 16 * n));
    }
}

static void compare(ArmShaderJitBackend* this, rasA64Reg dst, u8 op) {
    switch (op) {
        case 0:
            CSETW(dst, EQ);
            break;
        case 1:
            CSETW(dst, NE);
            break;
        case 2:
            CSETW(dst, LT);
            break;
        case 3:
            CSETW(dst, LE);
            break;
        case 4:
            CSETW(dst, GT);
            break;
        case 5:
            CSETW(dst, GE);
            break;
        default:
            MOVW(dst, 1);
            break;
    }
}

// true: NE is true condition, false: EQ is true condition
static bool condop(ArmShaderJitBackend* this, u32 op, bool refx, bool refy) {
    switch (op) {
        case 0:                 // OR
            if (refx && refy) { // x or y
                ORRW(R11, reg_cmpx, reg_cmpy);
                TSTW(R11, R11);
                return true;
            } else if (refx && !refy) { // x or !y == !(!x and y)
                BICSW(ZR, reg_cmpy, reg_cmpx);
                return false;
            } else if (!refx && refy) { // !x or y == !(x and !y)
                BICSW(ZR, reg_cmpx, reg_cmpy);
                return false;
            } else { // !x or !y == !(x and y)
                TSTW(reg_cmpx, reg_cmpy);
                return false;
            }
        case 1:                 // AND
            if (refx && refy) { // x and y
                TSTW(reg_cmpx, reg_cmpy);
                return true;
            } else if (refx && !refy) { // x and !y
                BICSW(ZR, reg_cmpx, reg_cmpy);
                return true;
            } else if (!refx && refy) { // !x and y
                BICSW(ZR, reg_cmpy, reg_cmpx);
                return true;
            } else { // !x AND !y == !(x or y)
                ORRW(R11, reg_cmpx, reg_cmpy);
                TSTW(R11, R11);
                return false;
            }
        case 2:
            TSTW(reg_cmpx, reg_cmpx);
            return refx;
        case 3:
            TSTW(reg_cmpy, reg_cmpy);
            return refy;
        default:
            TSTW(ZR, ZR);
            return false;
    }
}

// 0 * anything is 0 (ieee noncompliant)
// this is important to emulate
// zeros any lanes of src1 where src0 was 0
// modified src2 always goes into V1
static void setupMul(ArmShaderJitBackend* this, rasA64VReg src1, rasA64VReg src2) {
    FCMEQZ4S(V3, src1);
    BIC16B(V1, src2, V3);
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(this, v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,              \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(fmt) SRC(V0, 1, fmt)
#define SRC2(fmt) SRC(V1, 2, fmt)
#define SRC3(fmt) SRC(V2, 3, fmt)
#define GETDST(_fmt) getdest(instr.fmt##_fmt.dest, desc.destmask)
#define STRDST(_fmt) writedest(this, instr.fmt##_fmt.dest, desc.destmask)

static void compileBlock(ArmShaderJitBackend* this, ShaderUnit* shu, u32 start,
                         u32 len, bool isfunction) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
    u32 farthestjmp = 0;
    while (pc < end) {
        L(this->jmplabels.d[pc]);

        if (pc == start && isfunction) {
            PUSH(LR, ZR);
        }

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                FADD4S(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_DP3: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                MOVS(V1, 3, ZR); // set w to 0 to do a 3d dot product
                FMUL4S(dst, src1, V1);
                // risc moment
                // s[0] <- s[1]+s[2] AND s[1]<-s[2]+s[3]
                // then s[0] <- s[0]+s[1]
                FADDP4S(dst, dst, dst);
                FADDPS(dst, dst);
                if (desc.destmask != BIT(3 - 0)) {
                    DUP4S(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_DP4: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                FMUL4S(dst, src1, V1);
                // i love not having horizontal ADD
                FADDP4S(dst, dst, dst);
                FADDPS(dst, dst);
                // only DUP if writing to more than x
                if (desc.destmask != BIT(3 - 0)) {
                    DUP4S(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_DPH:
            case PICA_DPHI: {
                rasA64VReg src1, src2;
                if (instr.opcode == PICA_DPH) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                FMOVS(V3, 1.0f);
                if (src1.idx != 0) {
                    MOV16B(V0, src1);
                    src1 = V0; // need to move into V0 since modifying it
                }
                MOVS(src1, 3, V3, 0);
                setupMul(this, src1, src2);
                FMUL4S(dst, src1, V1);
                FADDP4S(dst, dst, dst);
                FADDPS(dst, dst);
                if (desc.destmask != BIT(3 - 0)) {
                    DUP4S(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_EX2: {
                this->usingex2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                FMOVS(V0, src);
                BL(this->ex2func);
                DUP4S(dst, V0, 0);
                STRDST(1);
                break;
            }
            case PICA_LG2: {
                this->usinglg2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                FMOVS(V0, src);
                BL(this->lg2func);
                DUP4S(dst, V0, 0);
                STRDST(1);
                break;
            }
            case PICA_MUL: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                FMUL4S(dst, src1, V1);
                STRDST(1);
                break;
            }
            case PICA_FLR: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                FRINTM4S(dst, src);
                STRDST(1);
                break;
            }
            case PICA_MIN: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                FMIN4S(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_MAX: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                FMAX4S(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_RCP: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                FRECPES(dst, src);
                if (desc.destmask != BIT(3 - 0)) {
                    DUP4S(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_RSQ: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                FRSQRTES(dst, src);
                if (desc.destmask != BIT(3 - 0)) {
                    DUP4S(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                rasA64VReg src1, src2;
                if (instr.opcode == PICA_SGE) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                FCMGE4S(dst, src1, src2);
                FMOV4S(V1, 1.0f);
                // sets to 1.0f if the condition was true
                AND16B(dst, dst, V1);
                STRDST(1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                rasA64VReg src1, src2;
                if (instr.opcode == PICA_SLT) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                // LT is just GT with operands reversed
                FCMGT4S(dst, src2, src1);
                FMOV4S(V1, 1.0f);
                AND16B(dst, dst, V1);
                STRDST(1);
                break;
            }
            case PICA_MOVA: {
                auto src = SRC1(1);
                FCVTZS2S(V0, src);
                if (desc.destmask & BIT(3 - 0)) {
                    MOVS(reg_ax, V0, 0);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    MOVS(reg_ay, V0, 1);
                }
                break;
            }
            case PICA_MOV: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                if (src.idx != dst.idx) {
                    MOV16B(dst, src);
                }
                STRDST(1);
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
                    POP(LR, ZR);
                    RET();
                    break;
                }
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                LABEL(lelse);
                if (instr.opcode == PICA_CALLU) {
                    TBZ(reg_b, instr.fmt3.c, lelse);
                } else if (instr.opcode == PICA_CALLC) {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        BEQ(lelse);
                    } else {
                        BNE(lelse);
                    }
                }
                BL(this->jmplabels.d[instr.fmt2.dest]);
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
                    TBZ(reg_b, instr.fmt3.c, lelse);
                } else {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        BEQ(lelse);
                    } else {
                        BNE(lelse);
                    }
                }

                compileBlock(this, shu, pc, instr.fmt2.dest - pc, false);
                if (instr.fmt2.num) {
                    B(lendif);
                    L(lelse);
                    compileBlock(this, shu, instr.fmt2.dest, instr.fmt2.num,
                                 false);
                    L(lendif);
                } else {
                    L(lelse);
                }

                pc = instr.fmt2.dest + instr.fmt2.num;
                break;
            }
            case PICA_LOOP: {
                LABEL(loop);

                PUSH(loopcount, ZR);
                MOVW(loopcount, 0);
                LDRB(reg_al, (reg_i, 4 * (instr.fmt3.c & 3) + 1));
                L(loop);

                compileBlock(this, shu, pc, instr.fmt3.dest + 1 - pc, false);

                LDRW(R11, (reg_i, 4 * (instr.fmt3.c & 3)));
                UBFXW(R12, R11, 16, 8);
                ADDW(reg_al, reg_al, R12);
                ADDW(loopcount, loopcount, 1);
                CMPW(loopcount, R11, UXTB());
                BLS(loop);

                POP(loopcount, ZR);

                pc = instr.fmt3.dest + 1;
                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                auto jmplab = this->jmplabels.d[instr.fmt3.dest];
                if (instr.opcode == PICA_JMPU) {
                    TSTW(reg_b, BIT(instr.fmt3.c));
                    if (instr.fmt2.num & 1) {
                        TBZ(reg_b, instr.fmt3.c, jmplab);
                    } else {
                        TBNZ(reg_b, instr.fmt3.c, jmplab);
                    }
                } else {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        BNE(jmplab);
                    } else {
                        BEQ(jmplab);
                    }
                }

                if (instr.fmt3.dest > farthestjmp)
                    farthestjmp = instr.fmt3.dest;
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                auto src1 = SRC1(1c);
                auto src2 = SRC2(1c);
                FCMPS(src1, src2);
                compare(this, reg_cmpx, instr.fmt1c.cmpx);
                MOVS(V0, 0, src1, 1);
                MOVS(V1, 0, src2, 1);
                FCMPS(V0, V1);
                compare(this, reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                auto src1 = SRC1(5);
                rasA64VReg src2, src3;
                if (instr.fmt5.opcode & 1) {
                    src2 = SRC2(5);
                    src3 = SRC3(5);
                } else {
                    src2 = SRC2(5i);
                    src3 = SRC3(5i);
                }
                auto dst = GETDST(5);

                setupMul(this, src1, src2);
                if (src3.idx != 2) MOV16B(V2, src3);
                FMLA4S(V2, src1, V1);
                MOV16B(dst, V2);
                STRDST(5);
                break;
            }
            default:
                lerror("unknown pica instr for JIT: %x (opcode %x)", instr.w,
                       instr.opcode);
        }
    }
}

static void compileEx2(ArmShaderJitBackend* this) {
    // takes x in s0 AND return in s0

    LABEL(end);

    // constants for getting good input to polynomials (found through testing)
    const float exthr = 0.535f;

    L(this->ex2func);
    // check nan
    FCMPS(V0, V0);
    BNE(end);

    // keep 1 somewhere
    FMOVS(V7, 1.f);

    // x = n + r where n in Z, r in [0,1)
    // 2^x = 2^n * 2^r, 2^r will be in [1,2)
    // so it ends up just being a float
    FRINTMS(V1, V0);
    FCVTMSSW(R11, V0);
    FSUBS(V0, V0, V1);
    // now n in w11 AND r in s0

    // translate from [0, 1) -> [exthr-1, exthr)
    MOVW(R17, F2I(exthr));
    FMOVW(V1, R17);
    FCMPS(V0, V1);
    FSUBS(V1, V0, V7);
    FCSELS(V0, V1, V0, HS);

    // make n into float exponent
    ADDW(R11, R11, 127);
    CMPW(R11, 0);
    CSELW(R11, ZR, R11, LT);
    MOVW(R12, 0xff);
    CMPW(R11, R12);
    CSELW(R11, R12, R11, GT);
    LSLW(R11, R11, 23);

    // 2^r = e^(r * ln2)
    // e^x ~= ((1/6 * x + 1/2) * x + 1) * x + 1
    const float c0 = M_LN2 * M_LN2 * M_LN2 / 6;
    const float c1 = M_LN2 * M_LN2 / 2;
    const float c2 = M_LN2;
    // c3 is just 1
    // now 2^x ~= ((c0 * x + c1) * x + c2) * x + c3

    MOVW(R17, F2I(c0));
    FMOVW(V1, R17);
    MOVW(R17, F2I(c1));
    FMOVW(V5, R17);
    MOVW(R17, F2I(c2));
    FMOVW(V6, R17);
    FMADDS(V1, V1, V0, V5);
    FMADDS(V1, V1, V0, V6);
    FMADDS(V1, V1, V0, V7);

    // EXTRACT the mantissa AND insert into the result
    FMOVW(R12, V1);
    BFIW(R11, R12, 0, 23);
    FMOVW(V0, R11);
    L(end);
    RET();
}

static void compileLg2(ArmShaderJitBackend* this) {
    // takes x in s0 AND return in s0

    // constants for getting good input to polynomials (found through testing)
    const float lgthr = 1.35f;

    LABEL(lnan);
    LABEL(lninf);

    L(this->lg2func);
    // check nan AND 0
    FCMPS(V0, V0);
    BNE(lnan);
    FCMPZS(V0);
    BEQ(lninf);

    // x = 2^n * r where n in Z AND r in [1,2)
    // log2(x) = n + log2(r)
    FMOVW(R11, V0);
    // check for negative number
    TBNZ(R11, 31, lnan);

    UBFXW(R12, R11, 23, 8);
    SUBW(R12, R12, 127);
    UBFXW(R11, R11, 0, 23);
    ORRW(R11, R11, 0x3f800000);
    FMOVW(V0, R11);
    // now n in w12 AND r in s0

    // translate from [1, 2) -> [lgthr/2, lgthr)
    MOVW(R17, F2I(lgthr));
    FMOVW(V1, R17);
    FCMPS(V0, V1);
    FMOVS(V7, 0.5f);
    FMULS(V1, V0, V7);
    FCSELS(V0, V1, V0, HS);
    CINCW(R12, R12, HS);

    // log2(r) = ln(r)/ln2
    // log polynomial is for x-1
    FMOVS(V7, 1.f);
    FSUBS(V0, V0, V7);

    // ln(x+1) ~= (((-1/4 * x + 1/3) * x - 1/2) * x + 1) * x
    const float c0 = -1 / (4 * M_LN2);
    const float c1 = 1 / (3 * M_LN2);
    const float c2 = -1 / (2 * M_LN2);
    const float c3 = 1 / M_LN2;
    // log2(x+1) ~= (((c0 * x + c1) * x + c2) * x + c3) * x

    MOVW(R17, F2I(c0));
    FMOVW(V1, R17);
    MOVW(R17, F2I(c1));
    FMOVW(V5, R17);
    MOVW(R17, F2I(c2));
    FMOVW(V6, R17);
    MOVW(R17, F2I(c3));
    FMOVW(V7, R17);
    FMADDS(V1, V1, V0, V5);
    FMADDS(V1, V1, V0, V6);
    FMADDS(V1, V1, V0, V7);
    FMULS(V1, V1, V0);

    // res is n + log r
    SCVTFSW(V0, R12);
    FADDS(V0, V0, V1);
    RET();

    // degenerate cases
    L(lnan);
    MOVW(R17, F2I(NAN));
    FMOVW(V0, R17);
    RET();
    L(lninf);
    MOVW(R17, F2I(-INFINITY));
    FMOVW(V0, R17);
    RET();
}

static rasLabel compileWithEntry(ArmShaderJitBackend* this, ShaderUnit* shu,
                                 u32 entry) {
    // this is so we only compile any functions that were not already compiled
    // while compiling previous entry points
    u32 callsStart = this->calls.size;

    LABEL(entrylab);
    L(entrylab);
    PUSH(LR, ZR);
    MOVX(R11, R0);
    ADDX(reg_v, R11, offsetof(ShaderUnit, v));
    ADDX(reg_o, R11, offsetof(ShaderUnit, o));
    LDRX(reg_c, (R11, offsetof(ShaderUnit, c)));
    LDRX(reg_i, (R11, offsetof(ShaderUnit, i)));
    LDRH(reg_b, (R11, offsetof(ShaderUnit, b)));

    compileBlock(this, shu, entry, SHADER_CODE_SIZE, false);

    POP(LR, ZR);
    RET();

    for (int i = callsStart; i < this->calls.size; i++) {
        auto inst = this->calls.d[i];
        compileBlock(this, shu, inst.fmt2.dest, inst.fmt2.num, true);
        POP(LR, ZR);
        RET();
    }

    return entrylab;
}

ShaderJitFunc shaderjit_arm_get_code(ArmShaderJitBackend* this,
                                     ShaderUnit* shu) {
    Vec_foreach(e, this->entrypoints) {
        if (e->pc == shu->entrypoint)
            return rasGetLabelAddr(this->code, e->lab);
    }
    // couldn't find this entrypoint, so recompile
    ArmShaderEntrypoint e = {shu->entrypoint, nullptr};
    int i = Vec_push(this->entrypoints, e);

    if (this->code) rasDestroy(this->code);
    this->code = rasCreate(16384);

    Vec_resize(this->jmplabels, SHADER_CODE_SIZE);
    for (int i = 0; i < SHADER_CODE_SIZE; i++) {
        this->jmplabels.d[i] = rasDeclareLabel(this->code);
    }
    this->ex2func = rasDeclareLabel(this->code);
    this->usingex2 = false;
    this->lg2func = rasDeclareLabel(this->code);
    this->usinglg2 = false;

    Vec_foreach(e, this->entrypoints) {
        e->lab = compileWithEntry(this, shu, e->pc);
    }

    if (this->usingex2) compileEx2(this);
    if (this->usinglg2) compileLg2(this);

    Vec_free(this->jmplabels);
    Vec_free(this->calls);

    rasReady(this->code);

#ifdef JIT_DISASM
    pica_shader_disasm(shu);
    shaderjit_arm_disassemble((void*) this);
#endif

    return rasGetLabelAddr(this->code, this->entrypoints.d[i].lab);
}

void shaderjit_arm_free(ArmShaderJitBackend* this) {
    if (!this) return;
    Vec_free(this->jmplabels);
    Vec_free(this->calls);
    Vec_free(this->entrypoints);
    rasDestroy(this->code);
    free(this);
}

#ifndef NOCAPSTONE
void shaderjit_arm_disassemble(ArmShaderJitBackend* this) {
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
    size_t count = cs_disasm(handle, rasGetCode(this->code),
                             rasGetSize(this->code), 0, 0, &insn);
    printf("--------- Shader JIT Disassembly at %p ------------\n",
           rasGetCode(this->code));
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %08x\t%s %s\n", insn[i].address, *(u32*) &insn[i].bytes,
               insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, count);
    cs_close(&handle);
}
#endif

#endif
