#ifdef __aarch64__

#include "shaderjit_arm.h"

#ifndef NOCAPSTONE
#include <capstone/capstone.h>
#endif
#include <math.h>

#define RAS_MACROS
#define RAS_CTX_VAR this->code
#include <ras/ras.h>

// #define JIT_DISASM

#define reg_v r0
#define reg_o r1
#define reg_r 16 // v16-v31
#define reg_c r2
#define reg_i r3
#define reg_b r4
#define reg_ax r5
#define reg_ay r6
#define reg_al r7
#define reg_cmpx r8
#define reg_cmpy r9
#define loopcount r10
// r11-r17 : temp
// v0-v7 : temp

ArmShaderJitBackend* shaderjit_arm_init() {
    return calloc(1, sizeof(ArmShaderJitBackend));
    // the ras code is initialized each time we have to recompile
    // for a new entrypoint
}

static rasVReg readsrc(ArmShaderJitBackend* this, rasVReg dst, u32 n, u8 idx,
                       u8 swizzle, bool neg) {
    rasVReg src = v3;
    if (swizzle == 0b00'01'10'11) {
        src = dst;
    }
    if (n < 0x10) {
        ldrq(src, (reg_v, 16 * n));
    } else if (n < 0x20) {
        n -= 0x10;
        src = VReg(reg_r + n);
    } else {
        n -= 0x20;
        if (idx == 0) {
            ldrq(src, (reg_c, 16 * n));
        } else {
            movw(r11, n);
            switch (idx) {
                case 1:
                    addw(r11, r11, reg_ax, sxtb());
                    break;
                case 2:
                    addw(r11, r11, reg_ay, sxtb());
                    break;
                case 3:
                    addw(r11, r11, reg_al, uxtb());
                    break;
            }
            andw(r11, r11, 0x7f);
            ldrq(src, (reg_c, r11, uxtw(4)));
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
        // use dup to transfer the most repeated
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
            dup4s(dst, src, maxidx);
        } else {
            maxidx = -1;
        }
        for (int i = 0; i < 4; i++) {
            if (swizzleidx[i] != maxidx) {
                movs(dst, i, src, swizzleidx[i]);
            }
        }
        if (neg) {
            fneg4s(dst, dst);
        }
        return dst;
    } else {
        if (neg) {
            fneg4s(dst, src);
            return dst;
        } else {
            return src;
        }
    }
}

// dest is either v0 or v16-v31
static rasVReg getdest(int n, u8 mask) {
    if (n >= 0x10 && mask == 0b1111) return VReg(reg_r + n - 0x10);
    else return v0;
}

// the result will be in v0
static void writedest(ArmShaderJitBackend* this, int n, u8 mask) {
    if (mask == 0b1111) {
        if (n < 0x10) {
            strq(v0, (reg_o, 16 * n));
        } else {
            // already handled by getdest
        }
    } else {
        // no blend on arm either :(
        if (n < 0x10) {
            // optimize storing single element mask to memory
            switch (mask) {
                case BIT(3 - 0):
                    strs(v0, (reg_o, 16 * n + 0));
                    return;
                case BIT(3 - 1):
                    movs(v0, 0, v0, 1);
                    strs(v0, (reg_o, 16 * n + 4));
                    return;
                case BIT(3 - 2):
                    movs(v0, 0, v0, 2);
                    strs(v0, (reg_o, 16 * n + 8));
                    return;
                case BIT(3 - 3):
                    movs(v0, 0, v0, 3);
                    strs(v0, (reg_o, 16 * n + 12));
                    return;
            }
        }
        rasVReg dst = v3;
        if (n < 0x10) {
            ldrq(v3, (reg_o, 16 * n));
        } else {
            n -= 0x10;
            dst = VReg(reg_r + n);
        }
        for (int i = 0; i < 4; i++) {
            if (mask & BIT(3 - i)) {
                movs(dst, i, v0, i);
            }
        }
        if (dst.idx == 3) strq(v3, (reg_o, 16 * n));
    }
}

static void compare(ArmShaderJitBackend* this, rasReg dst, u8 op) {
    switch (op) {
        case 0:
            csetw(dst, eq);
            break;
        case 1:
            csetw(dst, ne);
            break;
        case 2:
            csetw(dst, lt);
            break;
        case 3:
            csetw(dst, le);
            break;
        case 4:
            csetw(dst, gt);
            break;
        case 5:
            csetw(dst, ge);
            break;
        default:
            movw(dst, 1);
            break;
    }
}

// true: ne is true condition, false: eq is true condition
static bool condop(ArmShaderJitBackend* this, u32 op, bool refx, bool refy) {
    switch (op) {
        case 0:                 // OR
            if (refx && refy) { // x or y
                orrw(r11, reg_cmpx, reg_cmpy);
                tstw(r11, r11);
                return true;
            } else if (refx && !refy) { // x or !y == !(!x and y)
                bicsw(zr, reg_cmpy, reg_cmpx);
                return false;
            } else if (!refx && refy) { // !x or y == !(x and !y)
                bicsw(zr, reg_cmpx, reg_cmpy);
                return false;
            } else { // !x or !y == !(x and y)
                tstw(reg_cmpx, reg_cmpy);
                return false;
            }
        case 1:                 // AND
            if (refx && refy) { // x and y
                tstw(reg_cmpx, reg_cmpy);
                return true;
            } else if (refx && !refy) { // x and !y
                bicsw(zr, reg_cmpx, reg_cmpy);
                return true;
            } else if (!refx && refy) { // !x and y
                bicsw(zr, reg_cmpy, reg_cmpx);
                return true;
            } else { // !x and !y == !(x or y)
                orrw(r11, reg_cmpx, reg_cmpy);
                tstw(r11, r11);
                return false;
            }
        case 2:
            tstw(reg_cmpx, reg_cmpx);
            return refx;
        case 3:
            tstw(reg_cmpy, reg_cmpy);
            return refy;
        default:
            tstw(zr, zr);
            return false;
    }
}

// 0 * anything is 0 (ieee noncompliant)
// this is important to emulate
// zeros any lanes of src1 where src0 was 0
// modified src2 always goes into v1
static void setupMul(ArmShaderJitBackend* this, rasVReg src1, rasVReg src2) {
    fcmeqz4s(v3, src1);
    bic16b(v1, src2, v3);
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(this, v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,              \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(fmt) SRC(v0, 1, fmt)
#define SRC2(fmt) SRC(v1, 2, fmt)
#define SRC3(fmt) SRC(v2, 3, fmt)
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
            push(lr, zr);
        }

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fadd4s(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_DP3: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                movs(v1, 3, zr); // set w to 0 to do a 3d dot product
                fmul4s(dst, src1, v1);
                // risc moment
                // s[0] <- s[1]+s[2] and s[1]<-s[2]+s[3]
                // then s[0] <- s[0]+s[1]
                faddp4s(dst, dst, dst);
                faddps(dst, dst);
                if (desc.destmask != BIT(3 - 0)) {
                    dup4s(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_DP4: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                fmul4s(dst, src1, v1);
                // i love not having horizontal add
                faddp4s(dst, dst, dst);
                faddps(dst, dst);
                // only dup if writing to more than x
                if (desc.destmask != BIT(3 - 0)) {
                    dup4s(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_DPH:
            case PICA_DPHI: {
                rasVReg src1, src2;
                if (instr.opcode == PICA_DPH) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                fmovs(v3, 1.0f);
                if (src1.idx != 0) {
                    mov16b(v0, src1);
                    src1 = v0; // need to move into v0 since modifying it
                }
                movs(src1, 3, v3, 0);
                setupMul(this, src1, src2);
                fmul4s(dst, src1, v1);
                faddp4s(dst, dst, dst);
                faddps(dst, dst);
                if (desc.destmask != BIT(3 - 0)) {
                    dup4s(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_EX2: {
                this->usingex2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                fmovs(v0, src);
                bl(this->ex2func);
                dup4s(dst, v0, 0);
                STRDST(1);
                break;
            }
            case PICA_LG2: {
                this->usinglg2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                fmovs(v0, src);
                bl(this->lg2func);
                dup4s(dst, v0, 0);
                STRDST(1);
                break;
            }
            case PICA_MUL: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(this, src1, src2);
                fmul4s(dst, src1, v1);
                STRDST(1);
                break;
            }
            case PICA_FLR: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frintm4s(dst, src);
                STRDST(1);
                break;
            }
            case PICA_MIN: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fmin4s(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_MAX: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fmax4s(dst, src1, src2);
                STRDST(1);
                break;
            }
            case PICA_RCP: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frecpes(dst, src);
                if (desc.destmask != BIT(3 - 0)) {
                    dup4s(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_RSQ: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frsqrtes(dst, src);
                if (desc.destmask != BIT(3 - 0)) {
                    dup4s(dst, dst, 0);
                }
                STRDST(1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                rasVReg src1, src2;
                if (instr.opcode == PICA_SGE) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                fcmge4s(dst, src1, src2);
                fmov4s(v1, 1.0f);
                // sets to 1.0f if the condition was true
                and16b(dst, dst, v1);
                STRDST(1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                rasVReg src1, src2;
                if (instr.opcode == PICA_SLT) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                // lt is just gt with operands reversed
                fcmgt4s(dst, src2, src1);
                fmov4s(v1, 1.0f);
                and16b(dst, dst, v1);
                STRDST(1);
                break;
            }
            case PICA_MOVA: {
                auto src = SRC1(1);
                fcvtzs2s(v0, src);
                if (desc.destmask & BIT(3 - 0)) {
                    movs(reg_ax, v0, 0);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    movs(reg_ay, v0, 1);
                }
                break;
            }
            case PICA_MOV: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                if (src.idx != dst.idx) {
                    mov16b(dst, src);
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
                    pop(lr, zr);
                    ret();
                    break;
                }
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                Label(lelse);
                if (instr.opcode == PICA_CALLU) {
                    tbz(reg_b, instr.fmt3.c, lelse);
                } else if (instr.opcode == PICA_CALLC) {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        beq(lelse);
                    } else {
                        bne(lelse);
                    }
                }
                bl(this->jmplabels.d[instr.fmt2.dest]);
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
                Label(lelse);
                Label(lendif);
                if (instr.opcode == PICA_IFU) {
                    tbz(reg_b, instr.fmt3.c, lelse);
                } else {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        beq(lelse);
                    } else {
                        bne(lelse);
                    }
                }

                compileBlock(this, shu, pc, instr.fmt2.dest - pc, false);
                if (instr.fmt2.num) {
                    b(lendif);
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
                Label(loop);

                push(loopcount, zr);
                movw(loopcount, 0);
                ldrb(reg_al, (reg_i, 4 * (instr.fmt3.c & 3) + 1));
                L(loop);

                compileBlock(this, shu, pc, instr.fmt3.dest + 1 - pc, false);

                ldrw(r11, (reg_i, 4 * (instr.fmt3.c & 3)));
                ubfxw(r12, r11, 16, 8);
                addw(reg_al, reg_al, r12);
                addw(loopcount, loopcount, 1);
                cmpw(loopcount, r11, uxtb());
                bls(loop);

                pop(loopcount, zr);

                pc = instr.fmt3.dest + 1;
                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                auto jmplab = this->jmplabels.d[instr.fmt3.dest];
                if (instr.opcode == PICA_JMPU) {
                    tstw(reg_b, BIT(instr.fmt3.c));
                    if (instr.fmt2.num & 1) {
                        tbz(reg_b, instr.fmt3.c, jmplab);
                    } else {
                        tbnz(reg_b, instr.fmt3.c, jmplab);
                    }
                } else {
                    if (condop(this, instr.fmt2.op, instr.fmt2.refx,
                               instr.fmt2.refy)) {
                        bne(jmplab);
                    } else {
                        beq(jmplab);
                    }
                }

                if (instr.fmt3.dest > farthestjmp)
                    farthestjmp = instr.fmt3.dest;
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                auto src1 = SRC1(1c);
                auto src2 = SRC2(1c);
                fcmps(src1, src2);
                compare(this, reg_cmpx, instr.fmt1c.cmpx);
                movs(v0, 0, src1, 1);
                movs(v1, 0, src2, 1);
                fcmps(v0, v1);
                compare(this, reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                auto src1 = SRC1(5);
                rasVReg src2, src3;
                if (instr.fmt5.opcode & 1) {
                    src2 = SRC2(5);
                    src3 = SRC3(5);
                } else {
                    src2 = SRC2(5i);
                    src3 = SRC3(5i);
                }
                auto dst = GETDST(5);

                setupMul(this, src1, src2);
                if (src3.idx != 2) mov16b(v2, src3);
                fmla4s(v2, src1, v1);
                mov16b(dst, v2);
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
    // takes x in s0 and return in s0

    Label(end);

    // constants for getting good input to polynomials (found through testing)
    const float exthr = 0.535f;

    L(this->ex2func);
    // check nan
    fcmps(v0, v0);
    bne(end);

    // keep 1 somewhere
    fmovs(v7, 1.f);

    // x = n + r where n in Z, r in [0,1)
    // 2^x = 2^n * 2^r, 2^r will be in [1,2)
    // so it ends up just being a float
    frintms(v1, v0);
    fcvtmssw(r11, v0);
    fsubs(v0, v0, v1);
    // now n in w11 and r in s0

    // translate from [0, 1) -> [exthr-1, exthr)
    movw(r17, F2I(exthr));
    fmovw(v1, r17);
    fcmps(v0, v1);
    fsubs(v1, v0, v7);
    fcsels(v0, v1, v0, hs);

    // make n into float exponent
    addw(r11, r11, 127);
    cmpw(r11, 0);
    cselw(r11, zr, r11, lt);
    movw(r12, 0xff);
    cmpw(r11, r12);
    cselw(r11, r12, r11, gt);
    lslw(r11, r11, 23);

    // 2^r = e^(r * ln2)
    // e^x ~= ((1/6 * x + 1/2) * x + 1) * x + 1
    const float c0 = M_LN2 * M_LN2 * M_LN2 / 6;
    const float c1 = M_LN2 * M_LN2 / 2;
    const float c2 = M_LN2;
    // c3 is just 1
    // now 2^x ~= ((c0 * x + c1) * x + c2) * x + c3

    movw(r17, F2I(c0));
    fmovw(v1, r17);
    movw(r17, F2I(c1));
    fmovw(v5, r17);
    movw(r17, F2I(c2));
    fmovw(v6, r17);
    fmadds(v1, v1, v0, v5);
    fmadds(v1, v1, v0, v6);
    fmadds(v1, v1, v0, v7);

    // extract the mantissa and insert into the result
    fmovw(r12, v1);
    bfiw(r11, r12, 0, 23);
    fmovw(v0, r11);
    L(end);
    ret();
}

static void compileLg2(ArmShaderJitBackend* this) {
    // takes x in s0 and return in s0

    // constants for getting good input to polynomials (found through testing)
    const float lgthr = 1.35f;

    Label(lnan);
    Label(lninf);

    L(this->lg2func);
    // check nan and 0
    fcmps(v0, v0);
    bne(lnan);
    fcmpzs(v0);
    beq(lninf);

    // x = 2^n * r where n in Z and r in [1,2)
    // log2(x) = n + log2(r)
    fmovw(r11, v0);
    // check for negative number
    tbnz(r11, 31, lnan);

    ubfxw(r12, r11, 23, 8);
    subw(r12, r12, 127);
    ubfxw(r11, r11, 0, 23);
    orrw(r11, r11, 0x3f800000);
    fmovw(v0, r11);
    // now n in w12 and r in s0

    // translate from [1, 2) -> [lgthr/2, lgthr)
    movw(r17, F2I(lgthr));
    fmovw(v1, r17);
    fcmps(v0, v1);
    fmovs(v7, 0.5f);
    fmuls(v1, v0, v7);
    fcsels(v0, v1, v0, hs);
    cincw(r12, r12, hs);

    // log2(r) = ln(r)/ln2
    // log polynomial is for x-1
    fmovs(v7, 1.f);
    fsubs(v0, v0, v7);

    // ln(x+1) ~= (((-1/4 * x + 1/3) * x - 1/2) * x + 1) * x
    const float c0 = -1 / (4 * M_LN2);
    const float c1 = 1 / (3 * M_LN2);
    const float c2 = -1 / (2 * M_LN2);
    const float c3 = 1 / M_LN2;
    // log2(x+1) ~= (((c0 * x + c1) * x + c2) * x + c3) * x

    movw(r17, F2I(c0));
    fmovw(v1, r17);
    movw(r17, F2I(c1));
    fmovw(v5, r17);
    movw(r17, F2I(c2));
    fmovw(v6, r17);
    movw(r17, F2I(c3));
    fmovw(v7, r17);
    fmadds(v1, v1, v0, v5);
    fmadds(v1, v1, v0, v6);
    fmadds(v1, v1, v0, v7);
    fmuls(v1, v1, v0);

    // res is n + log r
    scvtfsw(v0, r12);
    fadds(v0, v0, v1);
    ret();

    // degenerate cases
    L(lnan);
    movw(r17, F2I(NAN));
    fmovw(v0, r17);
    ret();
    L(lninf);
    movw(r17, F2I(-INFINITY));
    fmovw(v0, r17);
    ret();
}

static rasLabel compileWithEntry(ArmShaderJitBackend* this, ShaderUnit* shu,
                                 u32 entry) {
    // this is so we only compile any functions that were not already compiled
    // while compiling previous entry points
    u32 callsStart = this->calls.size;

    Label(entrylab);
    L(entrylab);
    push(lr, zr);
    movx(r11, r0);
    addx(reg_v, r11, offsetof(ShaderUnit, v));
    addx(reg_o, r11, offsetof(ShaderUnit, o));
    ldrx(reg_c, (r11, offsetof(ShaderUnit, c)));
    ldrx(reg_i, (r11, offsetof(ShaderUnit, i)));
    ldrh(reg_b, (r11, offsetof(ShaderUnit, b)));

    compileBlock(this, shu, entry, SHADER_CODE_SIZE, false);

    pop(lr, zr);
    ret();

    for (int i = callsStart; i < this->calls.size; i++) {
        auto inst = this->calls.d[i];
        compileBlock(this, shu, inst.fmt2.dest, inst.fmt2.num, true);
        pop(lr, zr);
        ret();
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