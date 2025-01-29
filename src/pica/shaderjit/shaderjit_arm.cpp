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
        : Xbyak_aarch64::CodeGenerator(4096, Xbyak_aarch64::AutoGrow) {}

    u32 compileWithEntry(ShaderUnit* shu, u32 entry);
    void compileBlock(ShaderUnit* shu, u32 start, u32 len,
                      bool isfunction = false);

    void compileAllEntries(ShaderUnit* shu) {
        reset();
        jmplabels.clear();
        jmplabels.resize(SHADER_CODE_SIZE);
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
                fmul(dst.s4, src1.s4, v1.s4);
                // risc moment
                faddp(s1, dst.s2);
                mov(v0.s[0], dst.s[2]);
                fadd(s1, s1, s0);
                dup(dst.s4, v1.s[0]);
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
                faddp(s1, dst.s2);
                mov(dst.d[0], dst.d[1]);
                faddp(s2, dst.s2);
                fadd(s0, s1, s2);
                dup(dst.s4, v0.s[0]);
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
                faddp(s1, dst.s2);
                mov(dst.d[0], dst.d[1]);
                faddp(s2, dst.s2);
                fadd(s0, s1, s2);
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
                frecpe(s0, SReg(src.getIdx()));
                dup(dst.s4, v0.s[0]);
                STRDST(1);
                break;
            }
            case PICA_RSQ: {
                auto src = SRC1(1);
                auto dst = GETDST(1);
                frsqrte(s0, SReg(src.getIdx()));
                dup(dst.s4, v0.s[0]);
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
                and_(dst.b16, v1.b16, dst.b16);
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

                fcmge(dst.s4, src1.s4, src2.s4);
                fmov(v1.s4, 1.0f);
                bic(dst.b16, v1.b16, dst.b16);
                STRDST(1);
                break;
            }
            case PICA_MOVA: {
                auto src = SRC1(1);
                fcvtau(v0.s2, src.s2);
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
        printf("%04lx: %08x %s %s\n", insn[i].address, *(u32*) &insn[i].bytes,
               insn[i].mnemonic, insn[i].op_str);
    }
    cs_free(insn, count);
    cs_close(&handle);
}
}

#endif