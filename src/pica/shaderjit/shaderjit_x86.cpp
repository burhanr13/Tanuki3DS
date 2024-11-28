#include "shaderjit_x86.h"

#include <capstone/capstone.h>
#include <set>
#include <xbyak/xbyak.h>

// v in r10
// o in r11
// r : rbp
// scratch : xmm0, xmm1, xmm2
// xmm3 : negation mask
// xmm4 : ones
// c in rcx
// i in rdx
// b in rsi
// 16*a.x in r8
// 16*a.y in r9
// 16*al in rdi
// cmp.x in al
// cmp.y in ah
// rbx scratch

struct ShaderCode : Xbyak::CodeGenerator {

    ShaderCode(ShaderUnit* shu);

    void compileBlock(ShaderUnit* shu, u32 start, u32 len);

    void readsrc(Xbyak::Xmm dst, u32 n, u8 idx, u8 swizzle, bool neg) {
        Xbyak::Address a = xword[(int) 0];
        if (n < 0x10) {
            a = xword[r10 + 16 * n];
        } else if (n < 0x20) {
            n -= 0x10;
            a = xword[rbp + 16 * n];
        } else {
            n -= 0x20;
            switch (idx) {
                case 0:
                    a = xword[rcx + 16 * n];
                    break;
                case 1:
                    a = xword[rcx + 16 * n + r8];
                    break;
                case 2:
                    a = xword[rcx + 16 * n + r9];
                    break;
                case 3:
                    a = xword[rcx + 16 * n + rdi];
                    break;
            }
        }

        if (swizzle != 0b00011011) {
            // pica swizzles are backwards
            swizzle = (swizzle & 0xcc) >> 2 | (swizzle & 0x33) << 2;
            swizzle = (swizzle & 0xf0) >> 4 | (swizzle & 0x0f) << 4;
            pshufd(dst, a, swizzle);
        } else {
            movaps(dst, a);
        }
        if (neg) {
            xorps(dst, xmm3);
        }
    }

    void writedest(Xbyak::Xmm src, int n, u8 mask) {
        if (mask != 0b1111) {
            // pica destination masks are also backwards
            mask = (mask & 0b1010) >> 1 | (mask & 0b0101) << 1;
            mask = (mask & 0b1100) >> 2 | (mask & 0b0011) << 2;
            mask = ~mask & 0xf;
            if (n < 0x10) {
                blendps(src, xword[r11 + 16 * n], mask);
            } else {
                blendps(src, xword[rbp + 16 * (n - 0x10)], mask);
            }
        }
        if (n < 0x10) {
            movaps(xword[r11 + 16 * n], src);
        } else {
            n -= 0x10;
            movaps(xword[rbp + 16 * n], src);
        }
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

    // z flag set if condition is true
    void condop(u32 op, bool refx, bool refy) {
        mov(bx, ax);
        switch (op) {
            case 0:
                xor_(bl, refx);
                xor_(bh, refy);
                // 0 if equal, so er becomes and here
                and_(bl, bh);
                return;
            case 1:
                xor_(bl, refx);
                xor_(bh, refy);
                // similarly and becomes or
                or_(bl, bh);
                return;
            case 2:
                xor_(bl, refx);
                return;
            case 3:
                xor_(bh, refy);
                return;
            default:
                // we just need to set the z flag here
                xor_(bl, bl);
                return;
        }
    }

    // 0 * anything is 0 (ieee noncompliant)
    // this is important to emulate
    // zeros any lanes of xmm1 where xmm0 was 0
    void setupMul() {
        xorps(xmm5, xmm5);
        cmpneqps(xmm5, xmm0);
        andps(xmm1, xmm5);
    }
};

std::string procName(u32 start, u32 num) {
    return std::to_string(start) + "_" + std::to_string(num);
}

ShaderCode::ShaderCode(ShaderUnit* shu)
    : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow) {

    std::set<std::pair<u32, u32>> calls;

    for (int i = 0; i < SHADER_CODE_SIZE; i++) {
        PICAInstr callinst = shu->code[i];
        if (callinst.opcode == PICA_CALL || callinst.opcode == PICA_CALLC ||
            callinst.opcode == PICA_CALLU) {
            calls.insert({+callinst.fmt3.dest, +callinst.fmt3.num});
        }
    }

    push(rbp);
    push(rbx);
    sub(rsp, 8 + 16 * 16);
    mov(rbp, rsp);
    lea(r10, ptr[rdi + offsetof(ShaderUnit, v)]);
    lea(r11, ptr[rdi + offsetof(ShaderUnit, o)]);
    mov(rcx, qword[rdi + offsetof(ShaderUnit, c)]);
    mov(rdx, qword[rdi + offsetof(ShaderUnit, i)]);
    movzx(rsi, word[rdi + offsetof(ShaderUnit, b)]);
    mov(eax, BIT(31));
    movd(xmm3, eax);
    pshufd(xmm3, xmm3, 0);
    mov(eax, 0x3f800000); // 1.0f
    movd(xmm4, eax);
    pshufd(xmm4, xmm4, 0);

    compileBlock(shu, shu->entrypoint, SHADER_CODE_SIZE);

    add(rsp, 8 + 16 * 16);
    pop(rbx);
    pop(rbp);
    ret();

    for (auto [start, num] : calls) {
        L(procName(start, num));
        inLocalLabel();
        compileBlock(shu, start, num);
        outLocalLabel();
        ret();
    }

    ready();
}

#define SRC(v, i, _fmt)                                                        \
    readsrc(v, instr.fmt##_fmt.src##i, instr.fmt##_fmt.idx,                    \
            desc.src##i##swizzle, desc.src##i##neg)
#define SRC1(v, fmt) SRC(v, 1, fmt)
#define SRC2(v, fmt) SRC(v, 2, fmt)
#define SRC3(v, fmt) SRC(v, 3, fmt)
#define DEST(v, _fmt) writedest(v, instr.fmt##_fmt.dest, desc.destmask)

std::string jmpLabel(u32 pc) {
    return std::string(".") + std::to_string(pc);
}

void ShaderCode::compileBlock(ShaderUnit* shu, u32 start, u32 len) {
    u32 pc = start;
    u32 end = start + len;
    if (end > SHADER_CODE_SIZE) end = SHADER_CODE_SIZE;
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
            case PICA_MUL: {
                SRC1(xmm0, 1);
                SRC2(xmm1, 1);
                setupMul();
                mulps(xmm0, xmm1);
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
                pshufd(xmm0, xmm0, 0);
                DEST(xmm0, 1);
                break;
            }
            case PICA_RSQ: {
                SRC1(xmm0, 1);
                rsqrtss(xmm0, xmm0);
                pshufd(xmm0, xmm0, 0);
                DEST(xmm0, 1);
                break;
            }
            case PICA_MOVA: {
                SRC1(xmm0, 1);
                cvttps2dq(xmm0, xmm0);
                if (desc.destmask & BIT(3 - 0)) {
                    pextrd(r8d, xmm0, 0);
                    shl(r8, 4);
                }
                if (desc.destmask & BIT(3 - 1)) {
                    pextrd(r9d, xmm0, 1);
                    shl(r9, 4);
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
                return;
            case PICA_CALL:
            case PICA_CALLC:
            case PICA_CALLU: {
                inLocalLabel();
                if (instr.opcode == PICA_CALLU) {
                    test(rsi, BIT(instr.fmt3.c));
                    jz(".else");
                } else if (instr.opcode == PICA_CALLC) {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                    jnz(".else");
                }
                call(procName(instr.fmt2.dest, instr.fmt2.num));
                L(".else");
                outLocalLabel();
                break;
            }
            case PICA_IFU:
            case PICA_IFC: {
                inLocalLabel();

                if (instr.opcode == PICA_IFU) {
                    test(rsi, BIT(instr.fmt3.c));
                    jz(".else", T_NEAR);
                } else {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                    jnz(".else", T_NEAR);
                }

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
            case PICA_JMPC:
            case PICA_JMPU: {
                if (instr.opcode == PICA_JMPU) {
                    test(rsi, BIT(instr.fmt3.c));
                    if (instr.fmt3.num & 1) {
                        jz(jmpLabel(instr.fmt3.dest), T_NEAR);
                    } else {
                        jnz(jmpLabel(instr.fmt3.dest), T_NEAR);
                    }
                } else {
                    condop(instr.fmt2.op, instr.fmt2.refx, instr.fmt2.refy);
                    jz(jmpLabel(instr.fmt3.dest), T_NEAR);
                }
                break;
            }
            case PICA_CMP ... PICA_CMP + 1: {
                SRC1(xmm0, 1c);
                SRC2(xmm1, 1c);
                comiss(xmm0, xmm1);
                compare(al, instr.fmt1c.cmpx);
                psrldq(xmm0, 4);
                psrldq(xmm1, 4);
                comiss(xmm0, xmm1);
                compare(ah, instr.fmt1c.cmpy);
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

void* shaderjit_x86_compile(ShaderUnit* shu) {
    return (void*) new ShaderCode(shu);
}

ShaderJitFunc shaderjit_x86_get_code(void* backend) {
    return (ShaderJitFunc) ((ShaderCode*) backend)->getCode();
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
    printf("--------- Shader JIT Disassembly at 0x%p ------------\n",
           code->getCode());
    for (size_t i = 0; i < count; i++) {
        printf("%04lx: %s %s\n", insn[i].address, insn[i].mnemonic,
               insn[i].op_str);
    }
    cs_free(insn, count);
}
}