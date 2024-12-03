#include "shaderjit_x86.h"

#include <capstone/capstone.h>
#include <map>
#include <vector>
#include <xbyak/xbyak.h>

// #define JIT_DISASM

struct ShaderCode : Xbyak::CodeGenerator {

    Xbyak::Reg64 reg_v = r10;
    Xbyak::Reg64 reg_o = r11;
    Xbyak::Reg64 reg_r = rbp;
    Xbyak::Reg64 reg_c = rcx;
    Xbyak::Reg64 reg_i = rdx;
    Xbyak::Reg16 reg_b = si;
    Xbyak::Reg8 reg_ax = r8b;
    Xbyak::Reg8 reg_ay = r9b;
    Xbyak::Reg8 reg_al = dil;
    Xbyak::Reg8 reg_cmpx = al;
    Xbyak::Reg8 reg_cmpy = ah;
    Xbyak::Reg16 reg_cmp = ax;
    Xbyak::Xmm negmask = xmm3;
    Xbyak::Xmm ones = xmm4;
    Xbyak::Xmm tmp = xmm5;
    Xbyak::Reg8 loopcounter = r12b;

    std::vector<PICAInstr> calls;
    std::map<u32, u32> entrypoints;

    ShaderCode() : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow) {}

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
            shaderjit_x86_disassemble((void*) this);
#endif
        } else {
            offset = e->second;
        }
        return getCode() + offset;
    }

    void readsrc(Xbyak::Xmm dst, u32 n, u8 idx, u8 swizzle, bool neg) {
        Xbyak::Address addr = xword[(int) 0];
        if (n < 0x10) {
            addr = xword[reg_v + 16 * n];
        } else if (n < 0x20) {
            n -= 0x10;
            addr = xword[reg_r + 16 * n];
        } else {
            n -= 0x20;
            if (idx == 0) {
                addr = xword[reg_c + 16 * n];
            } else {
                switch (idx) {
                    case 1:
                        movzx(rbx, reg_ax);
                        break;
                    case 2:
                        movzx(rbx, reg_ay);
                        break;
                    case 3:
                        movzx(rbx, reg_al);
                        break;
                }
                add(rbx, n);
                and_(rbx, 0x7f);
                shl(rbx, 4);
                addr = xword[reg_c + rbx];
            }
        }

        if (swizzle != 0b00011011) {
            // pica swizzles are backwards
            swizzle = (swizzle & 0xcc) >> 2 | (swizzle & 0x33) << 2;
            swizzle = (swizzle & 0xf0) >> 4 | (swizzle & 0x0f) << 4;
            pshufd(dst, addr, swizzle);
        } else {
            movaps(dst, addr);
        }
        if (neg) {
            xorps(dst, negmask);
        }
    }

    void writedest(Xbyak::Xmm src, int n, u8 mask) {
        Xbyak::Address addr = xword[(int) 0];
        if (n < 0x10) {
            addr = xword[reg_o + 16 * n];
        } else {
            n -= 0x10;
            addr = xword[reg_r + 16 * n];
        }
        if (mask != 0b1111) {
            // pica destination masks are also backwards
            mask = (mask & 0b1010) >> 1 | (mask & 0b0101) << 1;
            mask = (mask & 0b1100) >> 2 | (mask & 0b0011) << 2;
            // invert the mask since we blend the opposite direction
            mask ^= 0xf;
            blendps(src, addr, mask);
        }
        movaps(addr, src);
    }

    void compare(Xbyak::Reg8 dst, u8 op) {
        switch (op) {
            case 0:
                sete(dst);
                break;
            case 1:
                setne(dst);
                break;
            case 2:
                setb(dst);
                break;
            case 3:
                setbe(dst);
                break;
            case 4:
                seta(dst);
                break;
            case 5:
                setae(dst);
                break;
            default:
                mov(dst, 1);
                break;
        }
    }

    // z flag clear if condition is true
    void condop(u32 op, bool refx, bool refy) {
        mov(bx, reg_cmp);
        switch (op) {
            case 0:
                if (!refx) xor_(bl, 1);
                if (!refy) xor_(bh, 1);
                or_(bl, bh);
                return;
            case 1:
                if (!refx) xor_(bl, 1);
                if (!refy) xor_(bh, 1);
                and_(bl, bh);
                return;
            case 2:
                if (refx) {
                    and_(bl, 1);
                } else {
                    xor_(bl, 1);
                }
                return;
            case 3:
                if (refy) {
                    and_(bh, 1);
                } else {
                    xor_(bh, 1);
                }
                return;
        }
    }

    // 0 * anything is 0 (ieee noncompliant)
    // this is important to emulate
    // zeros any lanes of xmm1 where xmm0 was 0
    void setupMul() {
        xorps(tmp, tmp);
        cmpneqps(tmp, xmm0);
        andps(xmm1, tmp);
    }
};

std::string endLabel(u32 entry) {
    return std::string("end") + std::to_string(entry);
}

// returns the offset of the function for the given entrypoint
u32 ShaderCode::compileWithEntry(ShaderUnit* shu, u32 entry) {
    u32 offset = getCurr() - getCode();

    // this is so we only compile any functions that were not already compiled
    // while compiling previous entry points
    u32 callsStart = calls.size();

    push(rbp);
    push(rbx);
    push(r12);
    sub(rsp, 16 * 16);
    mov(reg_r, rsp);
    lea(reg_v, ptr[rdi + offsetof(ShaderUnit, v)]);
    lea(reg_o, ptr[rdi + offsetof(ShaderUnit, o)]);
    mov(reg_c, qword[rdi + offsetof(ShaderUnit, c)]);
    mov(reg_i, qword[rdi + offsetof(ShaderUnit, i)]);
    mov(reg_b, word[rdi + offsetof(ShaderUnit, b)]);
    mov(eax, BIT(31));
    movd(negmask, eax);
    shufps(negmask, negmask, 0);
    mov(eax, 0x3f800000); // 1.0f
    movd(ones, eax);
    shufps(ones, ones, 0);

    compileBlock(shu, entry, SHADER_CODE_SIZE);

    L(endLabel(entry));
    add(rsp, 16 * 16);
    pop(r12);
    pop(rbx);
    pop(rbp);
    ret();

    for (size_t i = callsStart; i < calls.size(); i++) {
        compileBlock(shu, calls[i].fmt2.dest, calls[i].fmt2.num);
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

std::string jmpLabel(u32 pc) {
    return std::to_string(pc);
}

void ShaderCode::compileBlock(ShaderUnit* shu, u32 start, u32 len) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
    u32 farthestjmp = 0;
    while (pc < end) {
        L(jmpLabel(pc));

        PICAInstr instr = shu->code[pc++];
        OpDesc desc = shu->opdescs[instr.desc];
        switch (instr.opcode) {
            case PICA_ADD: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                addps(xmm0, xmm1);
                DEST(xmm0, 1);
                break;
            }
            case PICA_DP3: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                setupMul();
                dpps(xmm0, xmm1, 0x7f);
                DEST(xmm0, 1);
                break;
            }
            case PICA_DP4: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                setupMul();
                dpps(xmm0, xmm1, 0xff);
                DEST(xmm0, 1);
                break;
            }
            case PICA_DPH: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                insertps(xmm0, ones, 0 << 6 | 3 << 4); // src1[3] = 1
                setupMul();
                dpps(xmm0, xmm1, 0xff);
                DEST(xmm0, 1);
                break;
            }
            case PICA_MUL: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                setupMul();
                mulps(xmm0, xmm1);
                DEST(xmm0, 1);
                break;
            }
            case PICA_FLR: {
                SRC1(xmm0, 1);
                roundps(xmm0, xmm0, 1); // round towards -inf
                DEST(xmm0, 1);
                break;
            }
            case PICA_MIN: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                minps(xmm0, xmm1);
                DEST(xmm0, 1);
                break;
            }
            case PICA_MAX: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                maxps(xmm0, xmm1);
                DEST(xmm0, 1);
                break;
            }
            case PICA_RCP: {
                SRC1(xmm0, 1);
                rcpss(xmm0, xmm0);
                shufps(xmm0, xmm0, 0);
                DEST(xmm0, 1);
                break;
            }
            case PICA_RSQ: {
                SRC1(xmm0, 1);
                rsqrtss(xmm0, xmm0);
                shufps(xmm0, xmm0, 0);
                DEST(xmm0, 1);
                break;
            }
            case PICA_SGE:
            case PICA_SGEI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(xmm0, 1);
                    SRC2(xmm1, 1);
                } else {
                    SRC1(xmm0, 1i);
                    SRC2(xmm1, 1i);
                }
                cmpnltps(xmm0, xmm1);
                andps(xmm0, ones);
                DEST(xmm0, 1);
                break;
            }
            case PICA_SLT:
            case PICA_SLTI: {
                if (instr.opcode == PICA_SLT) {
                    SRC1(xmm0, 1);
                    SRC2(xmm1, 1);
                } else {
                    SRC1(xmm0, 1i);
                    SRC2(xmm1, 1i);
                }
                cmpltps(xmm0, xmm1);
                andps(xmm0, ones);
                DEST(xmm0, 1);
                break;
            }
            case PICA_MOVA: {
                SRC1(xmm0, 1);
                cvttps2dq(xmm0, xmm0);
                if (desc.destmask & BIT(3 - 0)) {
                    movd(reg_ax.cvt32(), xmm0);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    psrldq(xmm0, 4);
                    movd(reg_ay.cvt32(), xmm0);
                }
                break;
            }
            case PICA_MOV: {
                SRC1(xmm0, 1);
                DEST(xmm0, 1);
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
                    jmp(endLabel(shu->entrypoint), T_NEAR);
                    break;
                }
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                inLocalLabel();
                if (instr.opcode == PICA_CALLU) {
                    test(reg_b, BIT(instr.fmt3.c));
                } else if (instr.opcode == PICA_CALLC) {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                }
                if (instr.opcode != PICA_CALL) jz(".else");
                call(jmpLabel(instr.fmt2.dest));
                L(".else");
                outLocalLabel();

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
                inLocalLabel();

                if (instr.opcode == PICA_IFU) {
                    test(reg_b, BIT(instr.fmt3.c));
                } else {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                }
                jz(".else", T_NEAR);

                compileBlock(shu, pc, instr.fmt2.dest - pc);
                if (instr.fmt2.num) {
                    jmp(".endif", T_NEAR);
                    L(".else");
                    compileBlock(shu, instr.fmt2.dest, instr.fmt2.num);
                    L(".endif");
                } else {
                    L(".else");
                }

                pc = instr.fmt2.dest + instr.fmt2.num;
                outLocalLabel();
                break;
            }
            case PICA_LOOP: {
                inLocalLabel();

                push(loopcounter.cvt64());
                mov(loopcounter, 0);
                mov(reg_al, byte[reg_i + 4 * (instr.fmt3.c & 3) + 1]);
                L(".loop");

                compileBlock(shu, pc, instr.fmt3.dest + 1 - pc);

                add(reg_al, byte[reg_i + 4 * (instr.fmt3.c & 3) + 2]);
                inc(loopcounter);
                cmp(loopcounter, byte[reg_i + 4 * (instr.fmt3.c & 3) + 0]);
                jbe(".loop", T_NEAR);

                L(".break");
                pop(loopcounter.cvt64());

                pc = instr.fmt3.dest + 1;

                outLocalLabel();
                break;
            }
            case PICA_JMPC:
            case PICA_JMPU: {
                if (instr.opcode == PICA_JMPU) {
                    test(reg_b, BIT(instr.fmt3.c));
                    if (instr.fmt3.num & 1) {
                        jz(jmpLabel(instr.fmt3.dest), T_NEAR);
                    } else {
                        jnz(jmpLabel(instr.fmt3.dest), T_NEAR);
                    }
                } else {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                    jnz(jmpLabel(instr.fmt3.dest), T_NEAR);
                }
                if (instr.fmt3.dest > farthestjmp)
                    farthestjmp = instr.fmt3.dest;
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                SRC1(xmm0, 1c);
                SRC2(xmm1, 1c);
                comiss(xmm0, xmm1);
                compare(reg_cmpx, instr.fmt1c.cmpx);
                psrldq(xmm0, 4);
                psrldq(xmm1, 4);
                comiss(xmm0, xmm1);
                compare(reg_cmpy, instr.fmt1c.cmpy);
                break;
            }
            case PICA_MAD ... PICA_MAD + 0xf: {
                desc = shu->opdescs[instr.fmt5.desc];

                SRC1(xmm0, 5);
                if (instr.fmt5.opcode & 1) {
                    SRC2(xmm1, 5);
                    SRC3(xmm2, 5);
                } else {
                    SRC2(xmm1, 5i);
                    SRC3(xmm2, 5i);
                }

                setupMul();
                mulps(xmm0, xmm1);
                addps(xmm0, xmm2);

                DEST(xmm0, 5);
                break;
            }
            default:
                lerror("unknown pica instr for JIT: %x (opcode %x)", instr.w,
                       instr.opcode);
        }
    }
}

extern "C" {

void* shaderjit_x86_init() {
    return (void*) new ShaderCode();
}

ShaderJitFunc shaderjit_x86_get_code(void* backend, ShaderUnit* shu) {
    return (ShaderJitFunc) ((ShaderCode*) backend)->getCodeForEntry(shu);
}

void shaderjit_x86_free(void* backend) {
    delete ((ShaderCode*) backend);
}

void shaderjit_x86_disassemble(void* backend) {
    auto code = (ShaderCode*) backend;
    csh handle;
    cs_insn* insn;
    cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
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