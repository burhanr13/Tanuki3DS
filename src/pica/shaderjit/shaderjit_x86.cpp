#include <xbyak/xbyak.h>

#include "../shader.h"

// v in r10
// o in r11
// r : rsp
// scratch : xmm0 xmm1
// c in rcx
// i in rdx
// b in rsi[15:0]
// 16*a.x in rdi
// 16*a.y in r8
// 16*al in r9
// cmp in rsi[17:16]
// scratch rax

class ShaderCode : Xbyak::CodeGenerator {

    ShaderCode(ShaderUnit* shu);

    void readsrc(Xbyak::Xmm dst, u32 n, u8 idx, u8 swizzle, bool neg) {
        if (n < 0x10) {
            movaps(dst, xword[r10 + 16 * n]);
        } else if (n < 0x20) {
            n -= 0x10;
            movaps(dst, xword[rsp + 16 * n]);
        } else {
            n -= 0x20;
            switch (idx) {
                case 0:
                    movaps(dst, xword[rcx + 16 * n]);
                    break;
                case 1:
                    movaps(dst, xword[rcx + 16 * n + rdi]);
                    break;
                case 2:
                    movaps(dst, xword[rcx + 16 * n + r8]);
                    break;
                case 3:
                    movaps(dst, xword[rcx + 16 * n + r9]);
                    break;
            }
        }

        if (swizzle != 0b00011011) {
            swizzle = (swizzle & 0xcc) >> 2 | (swizzle & 0x33) << 2;
            swizzle = (swizzle & 0xf0) >> 4 | (swizzle & 0x0f) << 4;
            pshufd(dst, dst, swizzle);
        }
    }
};

ShaderCode::ShaderCode(ShaderUnit* shu)
    : Xbyak::CodeGenerator(4096, Xbyak::AutoGrow) {

    sub(rsp, 8 + 16 * 16);
    lea(r10, ptr[rdi + offsetof(ShaderUnit, v)]);
    lea(r11, ptr[rdi + offsetof(ShaderUnit, o)]);
    mov(rcx, qword[rdi + offsetof(ShaderUnit, c)]);
    mov(rdx, qword[rdi + offsetof(ShaderUnit, i)]);
    movzx(rsi, word[rdi + offsetof(ShaderUnit, b)]);

    add(rsp, 8 + 16 * 16);
    ret();
}