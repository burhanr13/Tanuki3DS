#ifdef __aarch64__

#include "shaderjit_arm.h"

#include <capstone/capstone.h>
#include <map>
#include <vector>
#include <xbyak_aarch64/xbyak_aarch64.h>

using namespace Xbyak_aarch64;

#define JIT_DISASM

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

    ShaderCode()
        : Xbyak_aarch64::CodeGenerator(4096, Xbyak_aarch64::AutoGrow),
          jmplabels(SHADER_CODE_SIZE) {}

    u32 compileWithEntry(ShaderUnit* shu, u32 entry);
    void compileBlock(ShaderUnit* shu, u32 start, u32 len);

    void compileAllEntries(ShaderUnit* shu) {
        reset();
        calls.clear();
        for (auto& e : entrypoints) {
            e.second = compileWithEntry(shu, e.first);
        }
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

    void readsrc(VReg dst, u32 n, u8 idx, u8 swizzle, bool neg) {
        VReg src = v3;
        if (n < 0x10) {
            ldr(q3, ptr(reg_v, 16 * n));
        } else if (n < 0x20) {
            n -= 0x10;
            src = VReg(reg_r + n);
        } else {
            n -= 0x20;
            if (idx == 0) {
                ldr(q3, ptr(reg_c, 16 * n));
            } else {
                switch (idx) {
                    case 1:
                        mov(w11, reg_ax);
                        break;
                    case 2:
                        mov(w11, reg_ay);
                        break;
                    case 3:
                        mov(w11, reg_al);
                        break;
                }
                add(w11, w11, n);
                and_(w11, w11, 0x7f);
                ldr(q3, ptr(reg_c, w11, UXTW, 4));
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
        } else {
            mov(dst.b16, src.b16);
        }
        if (neg) {
            fneg(dst.s4, dst.s4);
        }
    }

    void writedest(VReg src, int n, u8 mask) {
        if (mask == 0b1111) {
            if (n < 0x10) {
                str(QReg(src.getIdx()), ptr(reg_o, 16 * n));
            } else {
                n -= 0x10;
                mov(VReg(reg_r + n).b16, src.b16);
            }
        } else {
            // no blend on arm either :(
            if (n < 0x10) {
                // optimize single element mask
                switch (mask) {
                    case BIT(3 - 0):
                        str(SReg(src.getIdx()), ptr(reg_o, 16 * n + 0));
                        return;
                    case BIT(3 - 1):
                        mov(src.s[0], src.s[1]);
                        str(SReg(src.getIdx()), ptr(reg_o, 16 * n + 4));
                        return;
                    case BIT(3 - 2):
                        mov(src.s[0], src.s[2]);
                        str(SReg(src.getIdx()), ptr(reg_o, 16 * n + 8));
                        return;
                    case BIT(3 - 3):
                        mov(src.s[0], src.s[3]);
                        str(SReg(src.getIdx()), ptr(reg_o, 16 * n + 12));
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
                    mov(dst.s[i], src.s[i]);
                }
            }
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
                cset(dst, LO);
                break;
            case 3:
                cset(dst, LS);
                break;
            case 4:
                cset(dst, HI);
                break;
            case 5:
                cset(dst, HS);
                break;
            default:
                mov(dst, 1);
                break;
        }
    }

    // true: bne, false: beq
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
    // zeros any lanes of v1 where v0 was 0
    void setupMul() {
        fcmeq(v3.s4, v0.s4, 0);
        bic(v1.b16, v1.b16, v3.b16);
    }
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
        str(x30, pre_ptr(sp, -16));
        compileBlock(shu, calls[i].fmt2.dest, calls[i].fmt2.num);
        ldr(x30, post_ptr(sp, 16));
        ret();
    }

    return offset;
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                    \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(v, fmt) SRC(v, 1, fmt)
#define SRC2(v, fmt) SRC(v, 2, fmt)
#define SRC3(v, fmt) SRC(v, 3, fmt)
#define DEST(v, _fmt) writedest(v, instr.fmt##_fmt.dest, desc.destmask)

void ShaderCode::compileBlock(ShaderUnit* shu, u32 start, u32 len) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
    u32 farthestjmp = 0;
    while (pc < end) {
        L(jmplabels[pc]);

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                fadd(v0.s4, v0.s4, v1.s4);
                DEST(v0, 1);
                break;
            }
            case PICA_DP3: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                setupMul();
                fmul(v0.s4, v0.s4, v1.s4);
                // risc moment
                faddp(s1, v0.s2);
                mov(v0.s[0], v0.s[2]);
                fadd(s1, s1, s0);
                dup(v0.s4, v1.s[0]);
                DEST(v0, 1);
                break;
            }
            case PICA_DP4: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                setupMul();
                // i love not having horizontal add
                faddp(s1, v0.s2);
                mov(v0.d[0], v0.d[1]);
                faddp(s2, v0.s2);
                fadd(s0, s1, s2);
                dup(v0.s4, v0.s[0]);
                DEST(v0, 1);
                break;
            }
            case PICA_DPH:
            case PICA_DPHI: {
                if (instr.opcode == PICA_DPH) {
                    SRC1(v0, 1);
                    SRC2(v1, 1);
                } else {
                    SRC1(v0, 1i);
                    SRC2(v1, 1i);
                }
                fmov(s3, 1.0f);
                mov(v0.s[3], v3.s[0]);
                setupMul();
                faddp(s1, v0.s2);
                mov(v0.d[0], v0.d[1]);
                faddp(s2, v0.s2);
                fadd(s0, s1, s2);
                dup(v0.s4, v0.s[0]);
                DEST(v0, 1);
                break;
            }
            case PICA_MUL: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                setupMul();
                fmul(v0.s4, v0.s4, v1.s4);
                DEST(v0, 1);
                break;
            }
            case PICA_FLR: {
                SRC1(v0, 1);
                frintm(v0.s4, v0.s4);
                DEST(v0, 1);
                break;
            }
            case PICA_MIN: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                fmin(v0.s4, v0.s4, v1.s4);
                DEST(v0, 1);
                break;
            }
            case PICA_MAX: {
                SRC1(v0, 1);
                SRC2(v1, 1);
                fmax(v0.s4, v0.s4, v1.s4);
                DEST(v0, 1);
                break;
            }
            case PICA_RCP: {
                SRC1(v0, 1);
                frecpe(s0, s0);
                dup(v0.s4, v0.s[0]);
                DEST(v0, 1);
                break;
            }
            case PICA_RSQ: {
                SRC1(v0, 1);
                frsqrte(s0, s0);
                dup(v0.s4, v0.s[0]);
                DEST(v0, 1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(v0, 1);
                    SRC2(v1, 1);
                } else {
                    SRC1(v0, 1i);
                    SRC2(v1, 1i);
                }
                fcmge(v0.s4, v0.s4, v1.s4);
                fmov(v1.s4, 1.0f);
                and_(v0.b16, v0.b16, v1.b16);
                DEST(v0, 1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(v0, 1);
                    SRC2(v1, 1);
                } else {
                    SRC1(v0, 1i);
                    SRC2(v1, 1i);
                }
                fcmge(v0.s4, v0.s4, v1.s4);
                fmov(v1.s4, 1.0f);
                bic(v0.b16, v1.b16, v0.b16);
                DEST(v0, 1);
                break;
            }
            case PICA_MOVA: {
                SRC1(v0, 1);
                fcvtau(v0.s2, v0.s2);
                if (desc.destmask & BIT(3 - 0)) {
                    mov(reg_ax, v0.s[0]);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    mov(reg_ay, v0.s[1]);
                }
                break;
            }
            case PICA_MOV: {
                SRC1(v0, 1);
                DEST(v0, 1);
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
                    if (cond) bne(lelse);
                    else beq(lelse);
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
                if (cond) bne(lelse);
                else beq(lelse);

                compileBlock(shu, pc, instr.fmt2.dest - pc);
                if (instr.fmt2.num) {
                    b(lendif);
                    L(lelse);
                    compileBlock(shu, instr.fmt2.dest, instr.fmt2.num);
                    b(lendif);
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
                ubfx(w11, w11, 0, 8);
                cmp(loopcount, w11);
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
                SRC1(v0, 1c);
                SRC2(v1, 1c);
                fcmp(s0, s1);
                compare(reg_cmpx, instr.fmt1c.cmpx);
                mov(v0.s[0], v0.s[1]);
                mov(v1.s[0], v1.s[1]);
                fcmp(s0, s1);
                compare(reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                SRC1(v0, 5);
                if (instr.fmt5.opcode & 1) {
                    SRC2(v1, 5);
                    SRC3(v2, 5);
                } else {
                    SRC2(v1, 5i);
                    SRC3(v2, 5i);
                }

                setupMul();
                fmul(v0.s4, v0.s4, v1.s4);
                fadd(v0.s4, v0.s4, v2.s4);

                DEST(v0, 5);
                break;
            }
            default:
                lerror("unknown pica instr for JIT: %x (opcode %x)", instr.w,
                       instr.opcode);
        }
    }
};

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
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
}

#endif