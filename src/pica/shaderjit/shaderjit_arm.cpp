#ifdef __aarch64__

#include "shaderjit_arm.h"

#include <capstone/capstone.h>
#include <map>
#include <math.h>
#include <vector>
#include <xbyak_aarch64/xbyak_aarch64.h>

using namespace Xbyak_aarch64;

#undef F2I
#define F2I(i) (std::bit_cast<u32>(i))

// #define JIT_DISASM

struct ShaderCode : Xbyak_aarch64::CodeGenerator {

    XReg reg_v = x0;
    XReg reg_o = x1;
    int reg_r = 16; // v16-v31
    XReg reg_c = x2;
    XReg reg_i = x3;
    WReg reg_b = w4;
    WReg reg_ax = w5;
    WReg reg_ay = w6;
    WReg reg_al = w7;
    WReg reg_cmpx = w8;
    WReg reg_cmpy = w9;
    WReg loopcount = w10;
    // w11-w17 : temp
    // v0-v7 : temp

    std::vector<Label> jmplabels;
    std::vector<PICAInstr> calls;
    std::map<u32, u32> entrypoints;

    Label ex2func, lg2func;
    bool usingex2lg2;

    ShaderCode()
        : Xbyak_aarch64::CodeGenerator(4096, Xbyak_aarch64::AutoGrow) {}

    u32 compileWithEntry(ShaderUnit* shu, u32 entry);
    void compileBlock(ShaderUnit* shu, u32 start, u32 len,
                      bool isfunction = false);

    void compileAllEntries(ShaderUnit* shu) {
        reset();
        jmplabels.clear();
        jmplabels.resize(SHADER_CODE_SIZE);
        calls.clear();
        ex2func = Label();
        lg2func = Label();
        for (auto& e : entrypoints) {
            e.second = compileWithEntry(shu, e.first);
        }
        if (usingex2lg2) compileEx2Lg2();
        ready();
    }

    // it is possible to have multiple entrypoints in the same shader
    // we keep track of them and whenever there is a new one we recompile
    // the entire shader
    const u8* getCodeForEntry(ShaderUnit* shu) {
        auto e = entrypoints.find(shu->entrypoint);
        u32 offset;
        if (e == entrypoints.end()) {
            entrypoints[shu->entrypoint] = 0;
            compileAllEntries(shu);
            offset = entrypoints[shu->entrypoint];
#ifdef JIT_DISASM
            pica_shader_disasm(shu);
            shaderjit_arm_disassemble((void*) this);
#endif
        } else {
            offset = e->second;
        }
        return getCode() + offset;
    }

    VReg readsrc(VReg dst, u32 n, u8 idx, u8 swizzle, bool neg) {
        VReg src = v3;
        if (swizzle == 0b00'01'10'11) {
            src = dst;
        }
        if (n < 0x10) {
            ldr(QReg(src.getIdx()), ptr(reg_v, 16 * n));
        } else if (n < 0x20) {
            n -= 0x10;
            src = VReg(reg_r + n);
        } else {
            n -= 0x20;
            if (idx == 0) {
                ldr(QReg(src.getIdx()), ptr(reg_c, 16 * n));
            } else {
                switch (idx) {
                    case 1:
                        add(w11, reg_ax, n);
                        break;
                    case 2:
                        add(w11, reg_ay, n);
                        break;
                    case 3:
                        add(w11, reg_al, n);
                        break;
                }
                and_(w11, w11, 0x7f);
                ldr(QReg(src.getIdx()), ptr(reg_c, w11, UXTW, 4));
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
                dup(dst.s4, src.s[maxidx]);
            } else {
                maxidx = -1;
            }
            for (int i = 0; i < 4; i++) {
                if (swizzleidx[i] != maxidx) {
                    mov(dst.s[i], src.s[swizzleidx[i]]);
                }
            }
            if (neg) {
                fneg(dst.s4, dst.s4);
            }
            return dst;
        } else {
            if (neg) {
                fneg(dst.s4, src.s4);
                return dst;
            } else {
                return src;
            }
        }
    }

    // dest is either v0 or v16-v31
    VReg getdest(int n, u8 mask) {
        if (n >= 0x10 && mask == 0b1111) return VReg(reg_r + n - 0x10);
        else return v0;
    }

    // the result will be in v0
    void writedest(int n, u8 mask) {
        if (mask == 0b1111) {
            if (n < 0x10) {
                str(q0, ptr(reg_o, 16 * n));
            } else {
                // already handled by getdest
            }
        } else {
            // no blend on arm either :(
            if (n < 0x10) {
                // optimize storing single element mask to memory
                switch (mask) {
                    case BIT(3 - 0):
                        str(s0, ptr(reg_o, 16 * n + 0));
                        return;
                    case BIT(3 - 1):
                        mov(v0.s[0], v0.s[1]);
                        str(s0, ptr(reg_o, 16 * n + 4));
                        return;
                    case BIT(3 - 2):
                        mov(v0.s[0], v0.s[2]);
                        str(s0, ptr(reg_o, 16 * n + 8));
                        return;
                    case BIT(3 - 3):
                        mov(v0.s[0], v0.s[3]);
                        str(s0, ptr(reg_o, 16 * n + 12));
                        return;
                }
            }
            VReg dst = v3;
            if (n < 0x10) {
                ldr(q3, ptr(reg_o, 16 * n));
            } else {
                n -= 0x10;
                dst = VReg(reg_r + n);
            }
            for (int i = 0; i < 4; i++) {
                if (mask & BIT(3 - i)) {
                    mov(dst.s[i], v0.s[i]);
                }
            }
            if (dst.getIdx() == 3) str(q3, ptr(reg_o, 16 * n));
        }
    }

    void compare(WReg dst, u8 op) {
        switch (op) {
            case 0:
                cset(dst, EQ);
                break;
            case 1:
                cset(dst, NE);
                break;
            case 2:
                cset(dst, LT);
                break;
            case 3:
                cset(dst, LE);
                break;
            case 4:
                cset(dst, GT);
                break;
            case 5:
                cset(dst, GE);
                break;
            default:
                mov(dst, 1);
                break;
        }
    }

    // true: ne is true condition, false: eq is true condition
    bool condop(u32 op, bool refx, bool refy) {
        switch (op) {
            case 0:                 // OR
                if (refx && refy) { // x or y
                    orr(w11, reg_cmpx, reg_cmpy);
                    tst(w11, w11);
                    return true;
                } else if (refx && !refy) { // x or !y == !(!x and y)
                    bics(wzr, reg_cmpy, reg_cmpx);
                    return false;
                } else if (!refx && refy) { // !x or y == !(x and !y)
                    bics(wzr, reg_cmpx, reg_cmpy);
                    return false;
                } else { // !x or !y == !(x and y)
                    tst(reg_cmpx, reg_cmpy);
                    return false;
                }
            case 1:                 // AND
                if (refx && refy) { // x and y
                    tst(reg_cmpx, reg_cmpy);
                    return true;
                } else if (refx && !refy) { // x and !y
                    bics(wzr, reg_cmpx, reg_cmpy);
                    return true;
                } else if (!refx && refy) { // !x and y
                    bics(wzr, reg_cmpy, reg_cmpx);
                    return true;
                } else { // !x and !y == !(x or y)
                    orr(w11, reg_cmpx, reg_cmpy);
                    tst(w11, w11);
                    return false;
                }
            case 2:
                tst(reg_cmpx, reg_cmpx);
                return refx;
            case 3:
                tst(reg_cmpy, reg_cmpy);
                return refy;
            default:
                tst(wzr, wzr);
                return false;
        }
    }

    // 0 * anything is 0 (ieee noncompliant)
    // this is important to emulate
    // zeros any lanes of src1 where src0 was 0
    // modified src2 always goes into v1
    void setupMul(VReg src1, VReg src2) {
        fcmeq(v3.s4, src1.s4, 0);
        bic(v1.b16, src2.b16, v3.b16);
    }

    void compileEx2Lg2();
};

// returns the offset of the function for the given entrypoint
u32 ShaderCode::compileWithEntry(ShaderUnit* shu, u32 entry) {
    u32 offset = getCurr() - getCode();

    // this is so we only compile any functions that were not already compiled
    // while compiling previous entry points
    u32 callsStart = calls.size();

    str(x30, pre_ptr(sp, -16));
    mov(x11, x0);
    add(reg_v, x11, offsetof(ShaderUnit, v));
    add(reg_o, x11, offsetof(ShaderUnit, o));
    ldr(reg_c, ptr(x11, (u32) offsetof(ShaderUnit, c)));
    ldr(reg_i, ptr(x11, (u32) offsetof(ShaderUnit, i)));
    ldr(reg_b, ptr(x11, (u32) offsetof(ShaderUnit, b)));

    compileBlock(shu, entry, SHADER_CODE_SIZE);

    ldr(x30, post_ptr(sp, 16));
    ret();

    for (size_t i = callsStart; i < calls.size(); i++) {
        compileBlock(shu, calls[i].fmt2.dest, calls[i].fmt2.num, true);
        ldr(x30, post_ptr(sp, 16));
        ret();
    }

    return offset;
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                    \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(fmt) SRC(v0, 1, fmt)
#define SRC2(fmt) SRC(v1, 2, fmt)
#define SRC3(fmt) SRC(v2, 3, fmt)
#define GETDST(_fmt) getdest(instr.fmt##_fmt.dest, desc.destmask)
#define STRDST(_fmt) writedest(instr.fmt##_fmt.dest, desc.destmask)

void ShaderCode::compileBlock(ShaderUnit* shu, u32 start, u32 len,
                              bool isfunction) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
    u32 farthestjmp = 0;
    while (pc < end) {
        L(jmplabels[pc]);

        if (pc == start && isfunction) {
            // why cant this just be push {lr} like on arm32
            str(x30, pre_ptr(sp, -16));
        }

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fadd(dst.s4, src1.s4, src2.s4);
                STRDST(1);
                break;
            }
            case PICA_DP3: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(src1, src2);
                mov(v1.s[3], wzr); // set w to 0 to do a 3d dot product
                fmul(dst.s4, src1.s4, v1.s4);
                // risc moment
                // s[0] <- s[1]+s[2] and s[1]<-s[2]+s[3]
                // then s[0] <- s[0]+s[1]
                faddp(dst.s4, dst.s4, dst.s4);
                faddp(SReg(dst.getIdx()), dst.s2);
                if (desc.destmask != BIT(3 - 0)) {
                    dup(dst.s4, dst.s[0]);
                }
                STRDST(1);
                break;
            }
            case PICA_DP4: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(src1, src2);
                fmul(dst.s4, src1.s4, v1.s4);
                // i love not having horizontal add
                faddp(dst.s4, dst.s4, dst.s4);
                faddp(SReg(dst.getIdx()), dst.s2);
                // only dup if writing to more than x
                if (desc.destmask != BIT(3 - 0)) {
                    dup(dst.s4, dst.s[0]);
                }
                STRDST(1);
                break;
            }
            case PICA_DPH:
            case PICA_DPHI: {
                VReg src1(0), src2(0);
                if (instr.opcode == PICA_DPH) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                fmov(s3, 1.0f);
                if (src1.getIdx() != 0) {
                    mov(v0.b16, src1.b16);
                    src1 = v0; // need to move into v0 since modifying it
                }
                mov(src1.s[3], v3.s[0]);
                setupMul(src1, src2);
                fmul(dst.s4, src1.s4, v1.s4);
                faddp(dst.s4, dst.s4, dst.s4);
                faddp(SReg(dst.getIdx()), dst.s2);
                if (desc.destmask != BIT(3 - 0)) {
                    dup(dst.s4, dst.s[0]);
                }
                STRDST(1);
                break;
            }
            case PICA_EX2: {
                usingex2lg2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                mov(s0, src.s[0]);
                bl(ex2func);
                dup(dst.s4, v0.s[0]);
                STRDST(1);
                break;
            }
            case PICA_LG2: {
                usingex2lg2 = true;
                auto src = SRC1(1);
                auto dst = GETDST(1);
                mov(s0, src.s[0]);
                bl(lg2func);
                dup(dst.s4, v0.s[0]);
                STRDST(1);
                break;
            }
            case PICA_MUL: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                setupMul(src1, src2);
                fmul(dst.s4, src1.s4, v1.s4);
                STRDST(1);
                break;
            }
            case PICA_FLR: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frintm(dst.s4, src.s4);
                STRDST(1);
                break;
            }
            case PICA_MIN: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fmin(dst.s4, src1.s4, src2.s4);
                STRDST(1);
                break;
            }
            case PICA_MAX: {
                auto src1 = SRC1(1);
                auto src2 = SRC2(1);
                auto dst = GETDST(1);
                fmax(dst.s4, src1.s4, src2.s4);
                STRDST(1);
                break;
            }
            case PICA_RCP: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frecpe(SReg(dst.getIdx()), SReg(src.getIdx()));
                if (desc.destmask != BIT(3 - 0)) {
                    dup(dst.s4, dst.s[0]);
                }
                STRDST(1);
                break;
            }
            case PICA_RSQ: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frsqrte(SReg(dst.getIdx()), SReg(src.getIdx()));
                if (desc.destmask != BIT(3 - 0)) {
                    dup(dst.s4, dst.s[0]);
                }
                STRDST(1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                VReg src1(0), src2(0);
                if (instr.opcode == PICA_SGE) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                fcmge(dst.s4, src1.s4, src2.s4);
                fmov(v1.s4, 1.0f);
                // sets to 1.0f if the condition was true
                and_(dst.b16, dst.b16, v1.b16);
                STRDST(1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                VReg src1(0), src2(0);
                if (instr.opcode == PICA_SLT) {
                    src1 = SRC1(1);
                    src2 = SRC2(1);
                } else {
                    src1 = SRC1(1i);
                    src2 = SRC2(1i);
                }
                auto dst = GETDST(1);

                // lt is just gt with operands reversed
                fcmgt(dst.s4, src2.s4, src1.s4);
                fmov(v1.s4, 1.0f);
                and_(dst.b16, dst.b16, v1.b16);
                STRDST(1);
                break;
            }
            case PICA_MOVA: {
                auto src = SRC1(1);
                // this needs to be zs, or won't work
                fcvtzs(v0.s2, src.s2);
                if (desc.destmask & BIT(3 - 0)) {
                    mov(reg_ax, v0.s[0]);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    mov(reg_ay, v0.s[1]);
                }
                break;
            }
            case PICA_MOV: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                if (src.getIdx() != dst.getIdx()) {
                    mov(dst.b16, src.b16);
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
                    ldr(x30, post_ptr(sp, 16));
                    ret();
                    break;
                }
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                Label lelse;
                bool cond;
                if (instr.opcode == PICA_CALLU) {
                    tst(reg_b, BIT(instr.fmt3.c));
                    cond = true;
                } else if (instr.opcode == PICA_CALLC) {
                    cond =
                        condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                }
                if (instr.opcode != PICA_CALL) {
                    if (cond) beq(lelse);
                    else bne(lelse);
                }
                bl(jmplabels[instr.fmt2.dest]);
                L(lelse);

                bool found = false;
                for (auto call : calls) {
                    if (call.fmt2.dest == instr.fmt2.dest) {
                        found = true;
                        if (call.fmt2.num != instr.fmt2.num)
                            lerror("calling same function with different size");
                    }
                }
                if (!found) {
                    calls.push_back(instr);
                }
                break;
            }
            case PICA_IFU:
            case PICA_IFC: {
                Label lelse, lendif;
                bool cond;
                if (instr.opcode == PICA_IFU) {
                    tst(reg_b, BIT(instr.fmt3.c));
                    cond = true;
                } else {
                    cond =
                        condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                }
                if (cond) beq(lelse);
                else bne(lelse);

                compileBlock(shu, pc, instr.fmt2.dest - pc);
                if (instr.fmt2.num) {
                    b(lendif);
                    L(lelse);
                    compileBlock(shu, instr.fmt2.dest, instr.fmt2.num);
                    L(lendif);
                } else {
                    L(lelse);
                }

                pc = instr.fmt2.dest + instr.fmt2.num;
                break;
            }
            case PICA_LOOP: {
                Label loop;

                str(loopcount, pre_ptr(sp, -16));
                mov(loopcount, 0);
                ldrb(reg_al, ptr(reg_i, 4 * (instr.fmt3.c & 3) + 1));
                L(loop);

                compileBlock(shu, pc, instr.fmt3.dest + 1 - pc);

                ldr(w11, ptr(reg_i, 4 * (instr.fmt3.c & 3)));
                ubfx(w12, w11, 16, 8);
                add(reg_al, reg_al, w12);
                add(loopcount, loopcount, 1);
                cmp(loopcount, w11, UXTB);
                bls(loop);

                ldr(loopcount, post_ptr(sp, 16));

                pc = instr.fmt3.dest + 1;
                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                bool cond;
                if (instr.opcode == PICA_JMPU) {
                    tst(reg_b, BIT(instr.fmt3.c));
                    cond = !(instr.fmt2.num & 1);
                } else {
                    cond =
                        condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                }
                if (cond) bne(jmplabels[instr.fmt3.dest]);
                else beq(jmplabels[instr.fmt3.dest]);

                if (instr.fmt3.dest > farthestjmp)
                    farthestjmp = instr.fmt3.dest;
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                auto src1 = SRC1(1c);
                auto src2 = SRC2(1c);
                fcmp(SReg(src1.getIdx()), SReg(src2.getIdx()));
                compare(reg_cmpx, instr.fmt1c.cmpx);
                mov(v0.s[0], src1.s[1]);
                mov(v1.s[0], src2.s[1]);
                fcmp(s0, s1);
                compare(reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                auto src1 = SRC1(5);
                VReg src2(0), src3(0);
                if (instr.fmt5.opcode & 1) {
                    src2 = SRC2(5);
                    src3 = SRC3(5);
                } else {
                    src2 = SRC2(5i);
                    src3 = SRC3(5i);
                }
                auto dst = GETDST(5);

                setupMul(src1, src2);
                fmul(v0.s4, src1.s4, v1.s4);
                fadd(dst.s4, v0.s4, src3.s4);
                STRDST(5);
                break;
            }
            default:
                lerror("unknown pica instr for JIT: %x (opcode %x)", instr.w,
                       instr.opcode);
        }
    }
}

void ShaderCode::compileEx2Lg2() {
    // both take x in s0 and return in s0

    // constants for getting good input to polynomials (found through testing)
    Label Cexthr, Clgthr;
    // various numbers
    Label Cln2, C1_ln2, C1_6, C1_3, Cnan;

    Label nomodexp, nomodlog;
    Label nodegenlog;

    L(ex2func);
    // keep 1 somewhere
    fmov(s7, 1.f);

    // x = n + r where n in Z, r in [0,1)
    // 2^x = 2^n * 2^r, 2^r will be in [1,2)
    // so it ends up just being a float
    frintm(s1, s0);
    fcvtms(w11, s0);
    fsub(s0, s0, s1);
    // now n in w11 and r in s0

    // translate from [0, 1) -> [exthr-1, exthr)
    ldr(s1, Cexthr);
    fcmp(s0, s1);
    blt(nomodexp);
    fsub(s0, s0, s7);
    L(nomodexp);

    // make n into float exponent
    add(w11, w11, 127);
    cmp(w11, 0);
    csel(w11, wzr, w11, LT);
    mov(w12, 0xff);
    cmp(w11, w12);
    csel(w11, w12, w11, GT);
    lsl(w11, w11, 23);

    // 2^r = e^(r * ln2)
    ldr(s1, Cln2);
    fmul(s0, s0, s1);

    // e^x ~= ((1/6 * x + 1/2) * x + 1) * x + 1
    ldr(s1, C1_6);
    fmov(s6, 0.5f);
    fmadd(s1, s1, s0, s6);
    fmadd(s1, s1, s0, s7);
    fmadd(s1, s1, s0, s7);

    // extract the mantissa and insert into the result
    fmov(w12, s1);
    bfi(w11, w12, 0, 23);
    fmov(s0, w11);
    ret();

    L(lg2func);
    // x = 2^n * r where n in Z and r in [1,2)
    // log2(x) = n + log2(r)
    fmov(w11, s0);
    // check for negative number
    tbz(w11, 31, nodegenlog);
    ldr(s0, Cnan);
    ret();
    L(nodegenlog);
    ubfx(w12, w11, 23, 8);
    sub(w12, w12, 127);
    ubfx(w11, w11, 0, 23);
    orr(w11, w11, 0x3f800000);
    fmov(s0, w11);
    // now n in w12 and r in s0

    // translate from [1, 2) -> [lgthr/2, lgthr)
    ldr(s1, Clgthr);
    fcmp(s0, s1);
    blt(nomodlog);
    fmov(s7, 0.5f);
    fmul(s0, s0, s7);
    add(w12, w12, 1);
    L(nomodlog);

    // keep 1 here
    fmov(s7, 1.f);
    // log2(r) = ln(r)/ln2
    // log polynomial is for x-1
    fsub(s0, s0, s7);

    // ln(x+1) ~= (((-1/4 * x + 1/3) * x - 1/2) * x + 1) * x
    fmov(s1, -0.25f);
    ldr(s6, C1_3);
    fmadd(s1, s1, s0, s6);
    fmov(s6, 0.5f);
    fmadd(s1, s1, s0, s6);
    fmadd(s1, s1, s0, s7);
    fmul(s1, s1, s0);

    ldr(s2, C1_ln2);
    fmul(s1, s1, s2);

    // res is n + log r
    scvtf(s0, w12);
    fadd(s0, s0, s1);
    ret();

    L(Cexthr);
    dd(F2I(0.535f));
    L(Clgthr);
    dd(F2I(1.35f));
    L(Cln2);
    dd(F2I((float) M_LN2));
    L(C1_ln2);
    dd(F2I((float) (1.f / M_LN2)));
    L(C1_6);
    dd(F2I(1.f / 6.f));
    L(C1_3);
    dd(F2I(1.f / 3.f));
    L(Cnan);
    dd(0x7ff00000);
}

extern "C" {

void* shaderjit_arm_init() {
    return (void*) new ShaderCode();
}

ShaderJitFunc shaderjit_arm_get_code(void* backend, ShaderUnit* shu) {
    return (ShaderJitFunc) ((ShaderCode*) backend)->getCodeForEntry(shu);
}

void shaderjit_arm_free(void* backend) {
    delete ((ShaderCode*) backend);
}

void shaderjit_arm_disassemble(void* backend) {
    auto code = (ShaderCode*) backend;
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &handle);
    size_t count =
        cs_disasm(handle, code->getCode(), code->getSize(), 0, 0, &insn);
    printf("--------- Shader JIT Disassembly at %p ------------\n",
           code->getCode());
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %08x\t%s %s\n", insn[i].address, *(u32*) &insn[i].bytes,
               insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, count);
    cs_close(&handle);
}
}

#endif