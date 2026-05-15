#ifndef __RAS_MACROS_H
#define __RAS_MACROS_H

#include "ras.h"

#ifndef RAS_CTX_VAR
#define RAS_CTX_VAR ctx
#endif

#define __EMIT(name, ...) rasEmit##name(RAS_CTX_VAR, __VA_ARGS__)

#define __ID(...) __VA_ARGS__
#define __VA_IF_1(t, f, ...) t
#define __VA_IF_(t, f, ...) f
#define __VA_IF(t, f, ...) __VA_IF_##__VA_OPT__(1)(__ID(t), f, __VA_ARGS__)
#define __VA_DFL(dfl, ...) __VA_IF(__ID(__VA_ARGS__), dfl, __VA_ARGS__)

// unfortunately generic requires all branches to be well typed
// solve this by using more generic
// this solution using an undefined symbol is from
// https://www.chiark.greenend.org.uk/~sgtatham/quasiblog/c11-generic/#coercion
extern void* _ras_invalid_argument_type;
#define __FORCE_IMM(op)                                                        \
    _Generic(op,                                                               \
        rasReg: *(int*) _ras_invalid_argument_type,                            \
        rasVReg: *(int*) _ras_invalid_argument_type,                           \
        rasLabel: *(int*) _ras_invalid_argument_type,                          \
        default: op)
#define __FORCE(type, val)                                                     \
    _Generic(val, type: val, default: *(type*) _ras_invalid_argument_type)

#define ALIGN(a) rasAlign(RAS_CTX_VAR, a)

#define WORD(w) __EMIT(Word, w)
#define DWORD(d)                                                               \
    _Generic(d,                                                                \
        rasLabel: __EMIT(AbsAddr, __FORCE(rasLabel, d)),                       \
        default: __EMIT(Dword, __FORCE_IMM(d)))

#define ADDSUB(sf, op, s, rd, rn, op2, ...)                                    \
    _ADDSUB(sf, op, s, rd, rn, op2, __VA_DFL(LSL(0), __VA_ARGS__))
#define _ADDSUB(sf, op, s, rd, rn, op2, mod)                                   \
    _Generic(op2,                                                              \
        rasReg: _Generic(mod,                                                  \
            rasShift: __EMIT(AddSubShiftedReg, sf, op, s,                      \
                             __FORCE(rasShift, mod), __FORCE(rasReg, op2), rn, \
                             rd),                                              \
            default: __EMIT(AddSubExtendedReg, sf, op, s,                      \
                            __FORCE(rasExtend, mod), __FORCE(rasReg, op2), rn, \
                            rd)),                                              \
        default: _Generic(mod,                                                 \
            rasReg: __EMIT(PseudoAddSubImm, sf, op, s, rd, rn,                 \
                           __FORCE_IMM(op2), __FORCE(rasReg, mod)),            \
            default: __EMIT(AddSubImm, sf, op, s, __FORCE(rasShift, mod),      \
                            __FORCE_IMM(op2), rn, rd)))

#define ADDW(rd, rn, op2, ...) ADDSUB(0, 0, 0, rd, rn, op2, __VA_ARGS__)
#define ADDSW(rd, rn, op2, ...) ADDSUB(0, 0, 1, rd, rn, op2, __VA_ARGS__)
#define SUBW(rd, rn, op2, ...) ADDSUB(0, 1, 0, rd, rn, op2, __VA_ARGS__)
#define SUBSW(rd, rn, op2, ...) ADDSUB(0, 1, 1, rd, rn, op2, __VA_ARGS__)
#define ADDX(rd, rn, op2, ...) ADDSUB(1, 0, 0, rd, rn, op2, __VA_ARGS__)
#define ADDSX(rd, rn, op2, ...) ADDSUB(1, 0, 1, rd, rn, op2, __VA_ARGS__)
#define SUBX(rd, rn, op2, ...) ADDSUB(1, 1, 0, rd, rn, op2, __VA_ARGS__)
#define SUBSX(rd, rn, op2, ...) ADDSUB(1, 1, 1, rd, rn, op2, __VA_ARGS__)
#define CMPW(rn, op2, ...) SUBSW(ZR, rn, op2, __VA_ARGS__)
#define CMNW(rn, op2, ...) ADDSW(ZR, rn, op2, __VA_ARGS__)
#define NEGW(rd, rn, ...) SUBW(rd, ZR, rn, __VA_ARGS__)
#define NEGSW(rd, rn, ...) SUBSW(rd, ZR, rn, __VA_ARGS__)
#define CMPX(rn, op2, ...) SUBSX(ZR, rn, op2, __VA_ARGS__)
#define CMNX(rn, op2, ...) ADDSX(ZR, rn, op2, __VA_ARGS__)
#define NEGX(rd, rn, ...) SUBX(rd, ZR, rn, __VA_ARGS__)
#define NEGSX(rd, rn, ...) SUBSX(rd, ZR, rn, __VA_ARGS__)

#define ADDSUBCARRY(sf, op, s, rd, rn, rm)                                     \
    __EMIT(AddSubCarry, sf, op, s, rm, rn, rd)

#define ADCW(rd, rn, rm) ADDSUBCARRY(0, 0, 0, rd, rn, rm)
#define SBCW(rd, rn, rm) ADDSUBCARRY(0, 1, 0, rd, rn, rm)
#define ADCSW(rd, rn, rm) ADDSUBCARRY(0, 0, 1, rd, rn, rm)
#define SBCSW(rd, rn, rm) ADDSUBCARRY(0, 1, 1, rd, rn, rm)
#define ADCX(rd, rn, rm) ADDSUBCARRY(1, 0, 0, rd, rn, rm)
#define SBCX(rd, rn, rm) ADDSUBCARRY(1, 1, 0, rd, rn, rm)
#define ADCSX(rd, rn, rm) ADDSUBCARRY(1, 0, 1, rd, rn, rm)
#define SBCSX(rd, rn, rm) ADDSUBCARRY(1, 1, 1, rd, rn, rm)
#define NGCW(rd, rn) SBCW(rd, ZR, rn)
#define NGCSW(rd, rn) SBCSW(rd, ZR, rn)
#define NGCX(rd, rn) SBCX(rd, ZR, rn)
#define NGCSX(rd, rn) SBCSX(rd, ZR, rn)

#define __CINV(n, v) ((n) ? ~(v) : (v))

#define LOGICAL(sf, opc, n, rd, rn, op2, ...)                                  \
    _LOGICAL(sf, opc, n, rd, rn, op2, __VA_DFL(LSL(0), __VA_ARGS__))
#define _LOGICAL(sf, opc, n, rd, rn, op2, mod)                                 \
    _Generic(op2,                                                              \
        rasReg: __EMIT(LogicalReg, sf, opc, n, __FORCE(rasShift, mod),         \
                       __FORCE(rasReg, op2), rn, rd),                          \
        default: _Generic(mod,                                                 \
            rasReg: __EMIT(PseudoLogicalImm, sf, opc, rd, rn,                  \
                           __CINV(n, __FORCE_IMM(op2)), __FORCE(rasReg, mod)), \
            default: __EMIT(LogicalImm, sf, opc, __CINV(n, __FORCE_IMM(op2)),  \
                            rn, rd)))

#define ANDW(rd, rn, op2, ...) LOGICAL(0, 0, 0, rd, rn, op2, __VA_ARGS__)
#define BICW(rd, rn, op2, ...) LOGICAL(0, 0, 1, rd, rn, op2, __VA_ARGS__)
#define ORRW(rd, rn, op2, ...) LOGICAL(0, 1, 0, rd, rn, op2, __VA_ARGS__)
#define ORNW(rd, rn, op2, ...) LOGICAL(0, 1, 1, rd, rn, op2, __VA_ARGS__)
#define EORW(rd, rn, op2, ...) LOGICAL(0, 2, 0, rd, rn, op2, __VA_ARGS__)
#define EONW(rd, rn, op2, ...) LOGICAL(0, 2, 1, rd, rn, op2, __VA_ARGS__)
#define ANDSW(rd, rn, op2, ...) LOGICAL(0, 3, 0, rd, rn, op2, __VA_ARGS__)
#define BICSW(rd, rn, op2, ...) LOGICAL(0, 3, 1, rd, rn, op2, __VA_ARGS__)
#define ANDX(rd, rn, op2, ...) LOGICAL(1, 0, 0, rd, rn, op2, __VA_ARGS__)
#define BICX(rd, rn, op2, ...) LOGICAL(1, 0, 1, rd, rn, op2, __VA_ARGS__)
#define ORRX(rd, rn, op2, ...) LOGICAL(1, 1, 0, rd, rn, op2, __VA_ARGS__)
#define ORNX(rd, rn, op2, ...) LOGICAL(1, 1, 1, rd, rn, op2, __VA_ARGS__)
#define EORX(rd, rn, op2, ...) LOGICAL(1, 2, 0, rd, rn, op2, __VA_ARGS__)
#define EONX(rd, rn, op2, ...) LOGICAL(1, 2, 1, rd, rn, op2, __VA_ARGS__)
#define ANDSX(rd, rn, op2, ...) LOGICAL(1, 3, 0, rd, rn, op2, __VA_ARGS__)
#define BICSX(rd, rn, op2, ...) LOGICAL(1, 3, 1, rd, rn, op2, __VA_ARGS__)
#define MVNW(rn, rm) ORNW(rn, ZR, rm)
#define TSTW(rn, op2, ...) ANDSW(ZR, rn, op2, __VA_ARGS__)
#define MVNX(rn, rm) ORNX(rn, ZR, rm)
#define TSTX(rn, op2, ...) ANDSX(ZR, rn, op2, __VA_ARGS__)

#define DATAPROC1SOURCE(sf, s, opcode2, opcode, rd, rn)                        \
    __EMIT(DataProc1Source, sf, s, opcode2, opcode, rn, rd)

#define RBITW(rd, rn) DATAPROC1SOURCE(0, 0, 0, 0, rd, rn)
#define REV16W(rd, rn) DATAPROC1SOURCE(0, 0, 0, 1, rd, rn)
#define REVW(rd, rn) DATAPROC1SOURCE(0, 0, 0, 2, rd, rn)
#define CLZW(rd, rn) DATAPROC1SOURCE(0, 0, 0, 4, rd, rn)
#define CLSW(rd, rn) DATAPROC1SOURCE(0, 0, 0, 5, rd, rn)
#define RBITX(rd, rn) DATAPROC1SOURCE(1, 0, 0, 0, rd, rn)
#define REV16X(rd, rn) DATAPROC1SOURCE(1, 0, 0, 1, rd, rn)
#define REV32(rd, rn) DATAPROC1SOURCE(1, 0, 0, 2, rd, rn)
#define REVX(rd, rn) DATAPROC1SOURCE(1, 0, 0, 3, rd, rn)
#define CLZX(rd, rn) DATAPROC1SOURCE(1, 0, 0, 4, rd, rn)
#define CLSX(rd, rn) DATAPROC1SOURCE(1, 0, 0, 5, rd, rn)

#define DATAPROC2SOURCE(sf, s, opcode, rd, rn, rm)                             \
    __EMIT(DataProc2Source, sf, s, rm, opcode, rn, rd)

#define UDIVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 2, rd, rn, rm)
#define SDIVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 3, rd, rn, rm)
#define LSLVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 8, rd, rn, rm)
#define LSRVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 9, rd, rn, rm)
#define ASRVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 10, rd, rn, rm)
#define RORVW(rd, rn, rm) DATAPROC2SOURCE(0, 0, 11, rd, rn, rm)
#define UDIVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 2, rd, rn, rm)
#define SDIVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 3, rd, rn, rm)
#define LSLVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 8, rd, rn, rm)
#define LSRVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 9, rd, rn, rm)
#define ASRVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 10, rd, rn, rm)
#define RORVX(rd, rn, rm) DATAPROC2SOURCE(1, 0, 11, rd, rn, rm)

#define DATAPROC3SOURCE(sf, op54, op31, o0, rd, rn, rm, ra)                    \
    __EMIT(DataProc3Source, sf, op54, op31, rm, o0, ra, rn, rd)

#define MADDW(rd, rn, rm, ra) DATAPROC3SOURCE(0, 0, 0, 0, rd, rn, rm, ra)
#define MSUBW(rd, rn, rm, ra) DATAPROC3SOURCE(0, 0, 0, 1, rd, rn, rm, ra)
#define MADDX(rd, rn, rm, ra) DATAPROC3SOURCE(1, 0, 0, 0, rd, rn, rm, ra)
#define MSUBX(rd, rn, rm, ra) DATAPROC3SOURCE(1, 0, 0, 1, rd, rn, rm, ra)
#define SMADDL(rd, rn, rm, ra) DATAPROC3SOURCE(1, 0, 1, 0, rd, rn, rm, ra)
#define UMADDL(rd, rn, rm, ra) DATAPROC3SOURCE(1, 0, 5, 0, rd, rn, rm, ra)
#define MULW(rd, rn, rm) MADDW(rd, rn, rm, ZR)
#define MNEGW(rd, rn, rm) MSUBW(rd, rn, rm, ZR)
#define MULX(rd, rn, rm) MADDX(rd, rn, rm, ZR)
#define MNEGX(rd, rn, rm) MSUBX(rd, rn, rm, ZR)
#define SMULL(rd, rn, rm) SMADDL(rd, rn, rm, ZR)
#define UMULL(rd, rn, rm) UMADDL(rd, rn, rm, ZR)

#define CONDSELECT(sf, op, s, op2, rd, rn, rm, cond)                           \
    __EMIT(CondSelect, sf, op, s, rm, cond, op2, rn, rd)

#define CSELW(rd, rn, rm, cond) CONDSELECT(0, 0, 0, 0, rd, rn, rm, cond)
#define CSINCW(rd, rn, rm, cond) CONDSELECT(0, 0, 0, 1, rd, rn, rm, cond)
#define CSINVW(rd, rn, rm, cond) CONDSELECT(0, 1, 0, 0, rd, rn, rm, cond)
#define CSNEGW(rd, rn, rm, cond) CONDSELECT(0, 1, 0, 1, rd, rn, rm, cond)
#define CSELX(rd, rn, rm, cond) CONDSELECT(1, 0, 0, 0, rd, rn, rm, cond)
#define CSINCX(rd, rn, rm, cond) CONDSELECT(1, 0, 0, 1, rd, rn, rm, cond)
#define CSINVX(rd, rn, rm, cond) CONDSELECT(1, 1, 0, 0, rd, rn, rm, cond)
#define CSNEGX(rd, rn, rm, cond) CONDSELECT(1, 1, 0, 1, rd, rn, rm, cond)
#define CMOVW(rd, rm, cond) CSELW(rd, rm, rd, cond)
#define CSETW(rd, cond) CSINCW(rd, ZR, ZR, (cond) ^ 1)
#define CSETMW(rd, cond) CSINVW(rd, ZR, ZR, (cond) ^ 1)
#define CINVW(rd, rm, cond) CSINVW(rd, rm, rm, (cond) ^ 1)
#define CINCW(rd, rm, cond) CSINCW(rd, rm, rm, (cond) ^ 1)
#define CNEGW(rd, rm, cond) CSNEGW(rd, rm, rm, (cond) ^ 1)
#define CMOVX(rd, rm, cond) CSELX(rd, rm, rd, (cond) ^ 1)
#define CSETX(rd, cond) CSINCX(rd, ZR, ZR, (cond) ^ 1)
#define CSETMX(rd, cond) CSINVX(rd, ZR, ZR, (cond) ^ 1)
#define CINVX(rd, rm, cond) CSINVX(rd, rm, rm, (cond) ^ 1)
#define CINCX(rd, rm, cond) CSINCX(rd, rm, rm, (cond) ^ 1)
#define CNEGX(rd, rm, cond) CSNEGX(rd, rm, rm, (cond) ^ 1)

#define BITFIELD(sf, opc, n, rd, rn, immr, imms)                               \
    __EMIT(Bitfield, sf, opc, n, immr, imms, rn, rd)

#define SBFMW(rd, rn, immr, imms) BITFIELD(0, 0, 0, rd, rn, immr, imms)
#define BFMW(rd, rn, immr, imms) BITFIELD(0, 1, 0, rd, rn, immr, imms)
#define UBFMW(rd, rn, immr, imms) BITFIELD(0, 2, 0, rd, rn, immr, imms)
#define SBFMX(rd, rn, immr, imms) BITFIELD(1, 0, 1, rd, rn, immr, imms)
#define BFMX(rd, rn, immr, imms) BITFIELD(1, 1, 1, rd, rn, immr, imms)
#define UBFMX(rd, rn, immr, imms) BITFIELD(1, 2, 1, rd, rn, immr, imms)
#define SBFIZW(rd, rn, lsb, width) SBFMW(rd, rn, -(lsb) & 31, (width) - 1)
#define SBFXW(rd, rn, lsb, width) SBFMW(rd, rn, lsb, (lsb) + (width) - 1)
#define BFIW(rd, rn, lsb, width) BFMW(rd, rn, -(lsb) & 31, (width) - 1)
#define BFCW(rd, lsb, width) BFIW(rd, ZR, lsb, width)
#define BFXILW(rd, rn, lsb, width) BFMW(rd, rn, lsb, (lsb) + (width) - 1)
#define UBFIZW(rd, rn, lsb, width) UBFMW(rd, rn, -(lsb) & 31, (width) - 1)
#define UBFXW(rd, rn, lsb, width) UBFMW(rd, rn, lsb, (lsb) + (width) - 1)
#define SBFIZX(rd, rn, lsb, width) SBFMX(rd, rn, -(lsb) & 63, (width) - 1)
#define SBFXX(rd, rn, lsb, width) SBFMX(rd, rn, lsb, (lsb) + (width) - 1)
#define BFIX(rd, rn, lsb, width) BFMX(rd, rn, -(lsb) & 63, (width) - 1)
#define BFCX(rd, lsb, width) BFIX(rd, ZR, lsb, width)
#define BFXILX(rd, rn, lsb, width) BFMX(rd, rn, lsb, (lsb) + (width) - 1)
#define UBFIZX(rd, rn, lsb, width) UBFMX(rd, rn, -(lsb) & 63, (width) - 1)
#define UBFXX(rd, rn, lsb, width) UBFMX(rd, rn, lsb, (lsb) + (width) - 1)

#define EXTRACT(sf, op21, n, o0, rd, rn, rm, imms)                             \
    __EMIT(Extract, sf, op21, n, o0, rm, imms, rn, rd)

#define EXTRW(rd, rn, rm, imms) EXTRACT(0, 0, 0, 0, rd, rn, rm, imms)
#define EXTRX(rd, rn, rm, imms) EXTRACT(1, 0, 1, 0, rd, rn, rm, imms)

#define SHIFT(sf, type, rd, rn, op2)                                           \
    _Generic(op2,                                                              \
        rasReg: DATAPROC2SOURCE(sf, 0, 8 + type, rd, rn,                       \
                                __FORCE(rasReg, op2)),                         \
        default: __EMIT(PseudoShiftImm, sf, type, rd, rn, __FORCE_IMM(op2)))

#define LSLW(rd, rn, op2) SHIFT(0, 0, rd, rn, op2)
#define LSRW(rd, rn, op2) SHIFT(0, 1, rd, rn, op2)
#define ASRW(rd, rn, op2) SHIFT(0, 2, rd, rn, op2)
#define RORW(rd, rn, op2) SHIFT(0, 3, rd, rn, op2)
#define LSLX(rd, rn, op2) SHIFT(1, 0, rd, rn, op2)
#define LSRX(rd, rn, op2) SHIFT(1, 1, rd, rn, op2)
#define ASRX(rd, rn, op2) SHIFT(1, 2, rd, rn, op2)
#define RORX(rd, rn, op2) SHIFT(1, 3, rd, rn, op2)

#define _SHIFT(t, name, op, ...)                                               \
    __VA_IF(__SHIFT(name, op, __VA_DFL(0, __VA_ARGS__)), ((rasShift) {op, t}), \
            __VA_ARGS__)
#define __SHIFT(name, op, op1) ___SHIFT(name, op, op1)
#define ___SHIFT(name, op, op1, ...) name(op, op1, __VA_ARGS__)

#define LSL(op, ...) _SHIFT(0, LSL_, op __VA_OPT__(, ) __VA_ARGS__)
#define LSR(op, ...) _SHIFT(1, LSR_, op __VA_OPT__(, ) __VA_ARGS__)
#define ASR(op, ...) _SHIFT(2, ASR_, op __VA_OPT__(, ) __VA_ARGS__)

#define UXTB_(rd, rn) UBFXW(rd, rn, 0, 8)
#define UXTH_(rd, rn) UBFXW(rd, rn, 0, 16)
#define UXTW_(rd, rn) UBFXX(rd, rn, 0, 32)
#define SXTBW(rd, rn) SBFXW(rd, rn, 0, 8)
#define SXTBX(rd, rn) SBFXX(rd, rn, 0, 8)
#define SXTHW(rd, rn) SBFXW(rd, rn, 0, 16)
#define SXTHX(rd, rn) SBFXX(rd, rn, 0, 16)
#define SXTW_(rd, rn) SBFXX(rd, rn, 0, 32)

#define _EXTEND(t, name, ...) __EXTEND(t, name, __VA_DFL(0, __VA_ARGS__))
#define __EXTEND(t, name, op) ___EXTEND(t, name, op)
#define ___EXTEND(t, name, op, ...)                                            \
    __VA_IF(name(op, __VA_DFL(0, __VA_ARGS__)), ((rasExtend) {op, t}),         \
            __VA_ARGS__)

#define UXTB(...) _EXTEND(0, UXTB_, __VA_ARGS__)
#define UXTH(...) _EXTEND(1, UXTH_, __VA_ARGS__)
#define UXTW(...) _EXTEND(2, UXTW_, __VA_ARGS__)
#define UXTX(...) _EXTEND(3, MOVX, __VA_ARGS__)
#define SXTB(...) _EXTEND(4, SXTB_, __VA_ARGS__)
#define SXTH(...) _EXTEND(5, SXTH_, __VA_ARGS__)
#define SXTW(...) _EXTEND(6, SXTW_, __VA_ARGS__)
#define SXTX(...) _EXTEND(7, MOVX, __VA_ARGS__)

#define PCRELADDR(op, rd, l) __EMIT(PCRelAddr, op, l, rd)

#define ADR(rd, l) PCRELADDR(0, rd, l)
#define ADRP(rd, l) PCRELADDR(1, rd, l)
#define ADRL(rd, l) __EMIT(PseudoPCRelAddrLong, rd, l)

#define MOVEWIDE(sf, opc, rd, imm, ...)                                        \
    __EMIT(MoveWide, sf, opc, __VA_DFL(LSL(0), __VA_ARGS__), imm, rd)

#define MOVNW(rd, imm, ...) MOVEWIDE(0, 0, rd, imm, __VA_ARGS__)
#define MOVZW(rd, imm, ...) MOVEWIDE(0, 2, rd, imm, __VA_ARGS__)
#define MOVKW(rd, imm, ...) MOVEWIDE(0, 3, rd, imm, __VA_ARGS__)
#define MOVNX(rd, imm, ...) MOVEWIDE(1, 0, rd, imm, __VA_ARGS__)
#define MOVZX(rd, imm, ...) MOVEWIDE(1, 2, rd, imm, __VA_ARGS__)
#define MOVKX(rd, imm, ...) MOVEWIDE(1, 3, rd, imm, __VA_ARGS__)

#define MOVEGPR(sf, rd, op2)                                                   \
    _Generic(op2,                                                              \
        rasReg: __EMIT(PseudoMovReg, sf, rd, __FORCE(rasReg, op2)),            \
        default: __EMIT(PseudoMovImm, sf, rd, __FORCE_IMM(op2)))
#define MOVW(rd, op2) MOVEGPR(0, rd, op2)
#define MOVX(rd, op2) MOVEGPR(1, rd, op2)

#define __EXPAND_AMOD(amod) __EXPAND_AMOD1(__ID amod)
#define __EXPAND_AMOD1(amod) __EXPAND_AMOD2(amod)
#define __EXPAND_AMOD2(rn, ...) __EXPAND_AMOD3(rn, __VA_DFL(0, __VA_ARGS__))
#define __EXPAND_AMOD3(rn, off, ...) rn, off __VA_OPT__(, ) __VA_ARGS__

#define __MAKE_EXT(s)                                                          \
    _Generic(s,                                                                \
        rasShift: __EXT_OF_SHIFT(__FORCE(rasShift, s)),                        \
        rasExtend: s,                                                          \
        default: *(rasExtend*) _ras_invalid_argument_type)
#define __EXT_OF_SHIFT(s) ((rasExtend) {s.amt, 3, s.type != 0 || s.amt > 4})

#define __V2R(vn) _Generic(vn, rasVReg: REG((vn).idx))

#define LOADSTORE(vr, size, opc, rt, amod)                                     \
    _LOADSTORE(vr, size, opc, rt, __EXPAND_AMOD(amod))
#define _LOADSTORE(vr, size, opc, rt, amod) __LOADSTORE(vr, size, opc, rt, amod)
#define __LOADSTORE(vr, size, opc, rt, rn, off, ...)                           \
    _Generic(off,                                                              \
        rasReg: rasEmitLoadStoreRegOff,                                        \
        default: rasEmitLoadStoreImmOff)(                                      \
        RAS_CTX_VAR, size, vr, opc, off,                                       \
        _Generic(off,                                                          \
            rasReg: __MAKE_EXT(__VA_DFL(UXTX(), __VA_ARGS__)),                 \
            default: __VA_DFL(0, __VA_ARGS__)),                                \
        rn, rt)

#define STRB(rt, amod) LOADSTORE(0, 0, 0, rt, amod)
#define LDRB(rt, amod) LOADSTORE(0, 0, 1, rt, amod)
#define LDRSBX(rt, amod) LOADSTORE(0, 0, 2, rt, amod)
#define LDRSBW(rt, amod) LOADSTORE(0, 0, 3, rt, amod)
#define STRH(rt, amod) LOADSTORE(0, 1, 0, rt, amod)
#define LDRH(rt, amod) LOADSTORE(0, 1, 1, rt, amod)
#define LDRSHX(rt, amod) LOADSTORE(0, 1, 2, rt, amod)
#define LDRSHW(rt, amod) LOADSTORE(0, 1, 3, rt, amod)
#define STRW(rt, amod) LOADSTORE(0, 2, 0, rt, amod)
#define LDRW(rt, amod) LOADSTORE(0, 2, 1, rt, amod)
#define LDRSW(rt, amod) LOADSTORE(0, 2, 2, rt, amod)
#define STRX(rt, amod) LOADSTORE(0, 3, 0, rt, amod)
#define LDRX(rt, amod) LOADSTORE(0, 3, 1, rt, amod)

#define STRS(vt, amod) LOADSTORE(1, 2, 0, __V2R(vt), amod)
#define LDRS(vt, amod) LOADSTORE(1, 2, 1, __V2R(vt), amod)
#define STRD(vt, amod) LOADSTORE(1, 3, 0, __V2R(vt), amod)
#define LDRD(vt, amod) LOADSTORE(1, 3, 1, __V2R(vt), amod)
#define STRQ(vt, amod) LOADSTORE(1, 0, 2, __V2R(vt), amod)
#define LDRQ(vt, amod) LOADSTORE(1, 0, 3, __V2R(vt), amod)
#define PUSHV(vt) STRQ(vt, (SP, -0x10, PRE))
#define POPV(vt) LDRQ(vt, (SP, 0x10, POST))

#define LOADLITERAL(vr, opc, rt, l) __EMIT(LoadLiteral, opc, vr, l, rt)

#define LDRLW(rt, l) LOADLITERAL(0, 0, rt, l)
#define LDRLX(rt, l) LOADLITERAL(0, 1, rt, l)
#define LDRLSW(rt, l) LOADLITERAL(0, 2, rt, l)

#define LDRLS(vt, l) LOADLITERAL(1, 0, __V2R(vt), l)
#define LDRLD(vt, l) LOADLITERAL(1, 1, __V2R(vt), l)
#define LDRLQ(vt, l) LOADLITERAL(1, 2, __V2R(vt), l)

#define LOADSTOREPAIR(vr, opc, l, rt, rt2, amod)                               \
    _LOADSTOREPAIR(vr, opc, l, rt, rt2, __EXPAND_AMOD(amod))
#define _LOADSTOREPAIR(vr, opc, l, rt, rt2, amod)                              \
    __LOADSTOREPAIR(vr, opc, l, rt, rt2, amod)
#define __LOADSTOREPAIR(vr, opc, l, rt, rt2, rn, off, ...)                     \
    __EMIT(LoadStorePair, opc, vr, __VA_DFL(2, __VA_ARGS__), l, off, rt2, rn,  \
           rt)

#define STPW(rt, rt2, amod) LOADSTOREPAIR(0, 0, 0, rt, rt2, amod)
#define LDPW(rt, rt2, amod) LOADSTOREPAIR(0, 0, 1, rt, rt2, amod)
#define LDPSW(rt, rt2, amod) LOADSTOREPAIR(0, 1, 1, rt, rt2, amod)
#define STPX(rt, rt2, amod) LOADSTOREPAIR(0, 2, 0, rt, rt2, amod)
#define LDPX(rt, rt2, amod) LOADSTOREPAIR(0, 2, 1, rt, rt2, amod)
#define PUSH(rt, rt2) STPX(rt, rt2, (SP, -0x10, PRE))
#define POP(rt, rt2) LDPX(rt, rt2, (SP, 0x10, POST))

#define STPS(vt, vt2, amod) LOADSTOREPAIR(1, 0, 0, __V2R(vt), __V2R(vt2), amod)
#define LDPS(vt, vt2, amod) LOADSTOREPAIR(1, 0, 1, __V2R(vt), __V2R(vt2), amod)
#define STPD(vt, vt2, amod) LOADSTOREPAIR(1, 1, 0, __V2R(vt), __V2R(vt2), amod)
#define LDPD(vt, vt2, amod) LOADSTOREPAIR(1, 1, 1, __V2R(vt), __V2R(vt2), amod)
#define STPQ(vt, vt2, amod) LOADSTOREPAIR(1, 2, 0, __V2R(vt), __V2R(vt2), amod)
#define LDPQ(vt, vt2, amod) LOADSTOREPAIR(1, 2, 1, __V2R(vt), __V2R(vt2), amod)

#define POST 1
#define PRE 3

#define BRANCHUNCONDIMM(op, l) __EMIT(BranchUncondImm, op, l)
#define BRANCHCONDIMM(c, o0, l) __EMIT(BranchCondImm, l, o0, c)

#define B(l, ...)                                                              \
    __VA_IF(BRANCHCONDIMM(l, 0, __VA_ARGS__), BRANCHUNCONDIMM(0, l),           \
            __VA_ARGS__)
#define BL(l) BRANCHUNCONDIMM(1, l)

#define BRANCHCOMPIMM(sf, op, rt, l) __EMIT(BranchCompImm, sf, op, l, rt)

#define CBZW(rt, l) BRANCHCOMPIMM(0, 0, rt, l)
#define CBNZW(rt, l) BRANCHCOMPIMM(0, 1, rt, l)
#define CBZX(rt, l) BRANCHCOMPIMM(1, 0, rt, l)
#define CBNZX(rt, l) BRANCHCOMPIMM(1, 1, rt, l)

#define BRANCHTESTIMM(op, rt, B, l) __EMIT(BranchTestImm, op, B, l, rt)

#define TBZ(rt, B, l) BRANCHTESTIMM(0, rt, B, l)
#define TBNZ(rt, B, l) BRANCHTESTIMM(1, rt, B, l)

#define BRANCHREG(opc, op2, op3, op4, rn)                                      \
    __EMIT(BranchReg, opc, op2, op3, rn, op4)

#define BR(rn) BRANCHREG(0, 31, 0, 0, rn)
#define BLR(rn) BRANCHREG(1, 31, 0, 0, rn)
#define RET(...) BRANCHREG(2, 31, 0, 0, __VA_DFL(LR, __VA_ARGS__))

#define EQ 0
#define NE 1
#define CS 2
#define CC 3
#define MI 4
#define PL 5
#define VS 6
#define VC 7
#define HI 8
#define LS 9
#define GE 10
#define LT 11
#define GT 12
#define LE 13
#define AL 14
#define NV 15
#define HS CS
#define LO CC

#define BEQ(l) B(EQ, l)
#define BNE(l) B(NE, l)
#define BCS(l) B(CS, l)
#define BCC(l) B(CC, l)
#define BMI(l) B(MI, l)
#define BPL(l) B(PL, l)
#define BVS(l) B(VS, l)
#define BVC(l) B(VC, l)
#define BHI(l) B(HI, l)
#define BLS(l) B(LS, l)
#define BGE(l) B(GE, l)
#define BLT(l) B(LT, l)
#define BGT(l) B(GT, l)
#define BLE(l) B(LE, l)
#define BAL(l) B(AL, l)
#define BNV(l) B(NV, l)
#define BHS(l) B(HS, l)
#define BLO(l) B(LO, l)

#define HINT(opc) __EMIT(Hint, opc)
#define NOP() HINT(0)

#define SYSTEMREGMOVE(l, rt, opc) __EMIT(SystemRegMove, l, opc, rt)

#define MSR(opc, rt) SYSTEMREGMOVE(0, rt, opc)
#define MRS(rt, opc) SYSTEMREGMOVE(1, rt, opc)

#define LABEL(l, ...) rasLabel l = LNEW(__VA_ARGS__)
#define LNEW(...) __VA_IF(_LNEWEXT(__VA_ARGS__), _LNEW(), __VA_ARGS__)
#define _LNEW() rasDeclareLabel(RAS_CTX_VAR)
#define _LNEWEXT(addr) rasDefineLabelExternal(_LNEW(), addr)
#define L(l) rasDefineLabel(RAS_CTX_VAR, l)
#define LEXT(l, addr) rasDefineLabelExternal(l, addr)

#define FPMOVEIMM(ftype, m, s, rd, fimm, imm5)                                 \
    __EMIT(FPMoveImm, m, s, ftype, fimm, imm5, rd)

#define FPDATAPROC1SOURCE(ftype, m, s, opcode, rd, rn)                         \
    __EMIT(FPDataProc1Source, m, s, ftype, opcode, rn, rd)

#define FPMOVE(ftype, rd, op2)                                                 \
    _Generic(op2,                                                              \
        rasVReg: FPDATAPROC1SOURCE(ftype, 0, 0, 0, rd, __FORCE(rasVReg, op2)), \
        default: FPMOVEIMM(ftype, 0, 0, rd, __FORCE_IMM(op2), 0))
#define FMOVS(rd, op2) FPMOVE(0, rd, op2)
#define FMOVD(rd, op2) FPMOVE(1, rd, op2)

#define FABSS(rd, rn) FPDATAPROC1SOURCE(0, 0, 0, 1, rd, rn)
#define FNEGS(rd, rn) FPDATAPROC1SOURCE(0, 0, 0, 2, rd, rn)
#define FSQRTS(rd, rn) FPDATAPROC1SOURCE(0, 0, 0, 3, rd, rn)
#define FCVTDS(rd, rn) FPDATAPROC1SOURCE(0, 0, 0, 5, rd, rn)
#define FRINTMS(rd, rn) FPDATAPROC1SOURCE(0, 0, 0, 10, rd, rn)
#define FABSD(rd, rn) FPDATAPROC1SOURCE(1, 0, 0, 1, rd, rn)
#define FNEGD(rd, rn) FPDATAPROC1SOURCE(1, 0, 0, 2, rd, rn)
#define FSQRTD(rd, rn) FPDATAPROC1SOURCE(1, 0, 0, 3, rd, rn)
#define FCVTSD(rd, rn) FPDATAPROC1SOURCE(1, 0, 0, 4, rd, rn)

#define FPCOMPARE(ftype, m, s, op, opcode2, rn, rm)                            \
    __EMIT(FPCompare, m, s, ftype, rm, op, rn, opcode2)

#define FCMPS(rn, rm) FPCOMPARE(0, 0, 0, 0, 0, rn, rm)
#define FCMPZS(rn) FPCOMPARE(0, 0, 0, 0, 8, rn, V0)
#define FCMPD(rn, rm) FPCOMPARE(1, 0, 0, 0, 0, rn, rm)
#define FCMPZD(rn) FPCOMPARE(1, 0, 0, 0, 8, rn, V0)

#define FPDATAPROC2SOURCE(ftype, m, s, opcode, rd, rn, rm)                     \
    __EMIT(FPDataProc2Source, m, s, ftype, rm, opcode, rn, rd)

#define FMULS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 0, rd, rn, rm)
#define FDIVS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 1, rd, rn, rm)
#define FADDS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 2, rd, rn, rm)
#define FSUBS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 3, rd, rn, rm)
#define FMAXS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 4, rd, rn, rm)
#define FMINS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 5, rd, rn, rm)
#define FMAXNMS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 6, rd, rn, rm)
#define FMINNMS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 7, rd, rn, rm)
#define FNMULS(rd, rn, rm) FPDATAPROC2SOURCE(0, 0, 0, 8, rd, rn, rm)
#define FMULD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 0, rd, rn, rm)
#define FDIVD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 1, rd, rn, rm)
#define FADDD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 2, rd, rn, rm)
#define FSUBD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 3, rd, rn, rm)
#define FMAXD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 4, rd, rn, rm)
#define FMIND(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 5, rd, rn, rm)
#define FMAXNMD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 6, rd, rn, rm)
#define FMINNMD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 7, rd, rn, rm)
#define FNMULD(rd, rn, rm) FPDATAPROC2SOURCE(1, 0, 0, 8, rd, rn, rm)

#define FPDATAPROC3SOURCE(ftype, m, s, o1, o0, rd, rn, rm, ra)                 \
    __EMIT(FPDataProc3Source, m, s, ftype, o1, rm, o0, ra, rn, rd)

#define FMADDS(rd, rn, rm, ra) FPDATAPROC3SOURCE(0, 0, 0, 0, 0, rd, rn, rm, ra)
#define FMSUBS(rd, rn, rm, ra) FPDATAPROC3SOURCE(0, 0, 0, 0, 1, rd, rn, rm, ra)
#define FNMADDS(rd, rn, rm, ra) FPDATAPROC3SOURCE(0, 0, 0, 1, 0, rd, rn, rm, ra)
#define FNMSUBS(rd, rn, rm, ra) FPDATAPROC3SOURCE(0, 0, 0, 1, 1, rd, rn, rm, ra)
#define FMADDD(rd, rn, rm, ra) FPDATAPROC3SOURCE(1, 0, 0, 0, 0, rd, rn, rm, ra)
#define FMSUBD(rd, rn, rm, ra) FPDATAPROC3SOURCE(1, 0, 0, 0, 1, rd, rn, rm, ra)
#define FNMADDD(rd, rn, rm, ra) FPDATAPROC3SOURCE(1, 0, 0, 1, 0, rd, rn, rm, ra)
#define FNMSUBD(rd, rn, rm, ra) FPDATAPROC3SOURCE(1, 0, 0, 1, 1, rd, rn, rm, ra)

#define __R2V(vn) _Generic(vn, rasReg: VREG((vn).idx))

#define FPCONVERTINTRV(sf, ftype, s, rmode, opcode, rd, rn)                    \
    __EMIT(FPConvertInt, sf, s, ftype, rmode, opcode, rn, __R2V(rd))
#define FPCONVERTINTVR(sf, ftype, s, rmode, opcode, rd, rn)                    \
    __EMIT(FPConvertInt, sf, s, ftype, rmode, opcode, __R2V(rn), rd)

#define FPMOVEGPR(sf, rd, rn)                                                  \
    _Generic(rd,                                                               \
        rasReg: FPCONVERTINTRV(sf, sf, 0, 0, 6, __FORCE(rasReg, rd),           \
                               __FORCE(rasVReg, rn)),                          \
        rasVReg: FPCONVERTINTVR(sf, sf, 0, 0, 7, __FORCE(rasVReg, rd),         \
                                __FORCE(rasReg, rn)))
#define FMOVW(rd, rn) FPMOVEGPR(0, rd, rn)
#define FMOVX(rd, rn) FPMOVEGPR(1, rd, rn)

#define SCVTFSW(rd, rn) FPCONVERTINTVR(0, 0, 0, 0, 2, rd, rn)
#define UCVTFSW(rd, rn) FPCONVERTINTVR(0, 0, 0, 0, 3, rd, rn)
#define SCVTFDW(rd, rn) FPCONVERTINTVR(0, 1, 0, 0, 2, rd, rn)
#define UCVTFDW(rd, rn) FPCONVERTINTVR(0, 1, 0, 0, 3, rd, rn)
#define SCVTFSX(rd, rn) FPCONVERTINTVR(1, 0, 0, 0, 2, rd, rn)
#define UCVTFSX(rd, rn) FPCONVERTINTVR(1, 0, 0, 0, 3, rd, rn)
#define SCVTFDX(rd, rn) FPCONVERTINTVR(1, 1, 0, 0, 2, rd, rn)
#define UCVTFDX(rd, rn) FPCONVERTINTVR(1, 1, 0, 0, 3, rd, rn)

#define FCVTMSSW(rd, rn) FPCONVERTINTRV(0, 0, 0, 2, 0, rd, rn)
#define FCVTZSSW(rd, rn) FPCONVERTINTRV(0, 0, 0, 3, 0, rd, rn)
#define FCVTZUSW(rd, rn) FPCONVERTINTRV(0, 0, 0, 3, 1, rd, rn)
#define FCVTZSDW(rd, rn) FPCONVERTINTRV(0, 1, 0, 3, 0, rd, rn)
#define FCVTZUDW(rd, rn) FPCONVERTINTRV(0, 1, 0, 3, 1, rd, rn)
#define FCVTZSSX(rd, rn) FPCONVERTINTRV(1, 0, 0, 3, 0, rd, rn)
#define FCVTZUSX(rd, rn) FPCONVERTINTRV(1, 0, 0, 3, 1, rd, rn)
#define FCVTZSDX(rd, rn) FPCONVERTINTRV(1, 1, 0, 3, 0, rd, rn)
#define FCVTZUDX(rd, rn) FPCONVERTINTRV(1, 1, 0, 3, 1, rd, rn)

#define FPCONDSELECT(ftype, m, s, rd, rn, rm, cond)                            \
    __EMIT(FPCondSelect, m, s, ftype, rm, cond, rn, rd)

#define FCSELS(rd, rn, rm, cond) FPCONDSELECT(0, 0, 0, rd, rn, rm, cond)
#define FCSELD(rd, rn, rm, cond) FPCONDSELECT(1, 0, 0, rd, rn, rm, cond)

#define ADVSIMDCOPY(q, op, imm5, imm4, rd, rn)                                 \
    __EMIT(AdvSIMDCopy, q, op, imm5, imm4, rn, rd)

#define DUP(q, sz, rd, rn, idx)                                                \
    _Generic(rn,                                                               \
        rasVReg: ADVSIMDCOPY(q, 0, 1 << (sz) | (idx) << ((sz) + 1), 0, rd,     \
                             __FORCE(rasVReg, rn)),                            \
        rasReg: ADVSIMDCOPY(q, 0, 1 << (sz), 1, rd,                            \
                            __R2V(__FORCE(rasReg, rn))))
#define DUP8B(rd, rn, ...) DUP(0, 0, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP16B(rd, rn, ...) DUP(1, 0, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP4H(rd, rn, ...) DUP(0, 1, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP8H(rd, rn, ...) DUP(1, 1, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP2S(rd, rn, ...) DUP(0, 2, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP4S(rd, rn, ...) DUP(1, 2, rd, rn, __VA_DFL(0, __VA_ARGS__))
#define DUP2D(rd, rn, ...) DUP(1, 3, rd, rn, __VA_DFL(0, __VA_ARGS__))

#define MOVEELEM(sf, sz, u, rd, rn, idx)                                       \
    ADVSIMDCOPY(sf, 0, 1 << (sz) | (idx) << ((sz) + 1), u ? 7 : 5, __R2V(rd),  \
                rn)
#define SMOVBW(rd, rn, idx) MOVEELEM(0, 0, 0, rd, rn, idx)
#define UMOVBW(rd, rn, idx) MOVEELEM(0, 0, 1, rd, rn, idx)
#define SMOVHW(rd, rn, idx) MOVEELEM(0, 1, 0, rd, rn, idx)
#define UMOVHW(rd, rn, idx) MOVEELEM(0, 1, 1, rd, rn, idx)
#define SMOVSW(rd, rn, idx) MOVEELEM(0, 2, 0, rd, rn, idx)
#define UMOVSW(rd, rn, idx) MOVEELEM(0, 2, 1, rd, rn, idx)
#define SMOVBX(rd, rn, idx) MOVEELEM(1, 0, 0, rd, rn, idx)
#define UMOVBX(rd, rn, idx) MOVEELEM(1, 0, 1, rd, rn, idx)
#define SMOVHX(rd, rn, idx) MOVEELEM(1, 1, 0, rd, rn, idx)
#define UMOVHX(rd, rn, idx) MOVEELEM(1, 1, 1, rd, rn, idx)
#define SMOVSX(rd, rn, idx) MOVEELEM(1, 2, 0, rd, rn, idx)
#define UMOVSX(rd, rn, idx) MOVEELEM(1, 2, 1, rd, rn, idx)
#define SMOVD(rd, rn, idx) MOVEELEM(1, 3, 0, rd, rn, idx)
#define UMOVD(rd, rn, idx) MOVEELEM(1, 3, 1, rd, rn, idx)

#define INS(sz, rd, idx1, rn, idx2)                                            \
    _Generic(rn,                                                               \
        rasVReg: ADVSIMDCOPY(1, 1, 1 << (sz) | (idx1) << ((sz) + 1),           \
                             (idx2) << (sz), rd, __FORCE(rasVReg, rn)),        \
        rasReg: ADVSIMDCOPY(1, 0, 1 << (sz) | (idx1) << ((sz) + 1), 3, rd,     \
                            __R2V(__FORCE(rasReg, rn))))
#define INSB(rd, idx1, rn, ...) INS(0, rd, idx1, rn, __VA_DFL(0, __VA_ARGS__))
#define INSH(rd, idx1, rn, ...) INS(1, rd, idx1, rn, __VA_DFL(0, __VA_ARGS__))
#define INSS(rd, idx1, rn, ...) INS(2, rd, idx1, rn, __VA_DFL(0, __VA_ARGS__))
#define INSD(rd, idx1, rn, ...) INS(3, rd, idx1, rn, __VA_DFL(0, __VA_ARGS__))

#define MOVB(rd, idx1, rn, ...) INSB(rd, idx1, rn, __VA_OPT__(, ) __VA_ARGS__)
#define MOVH(rd, idx1, rn, ...) INSH(rd, idx1, rn, __VA_OPT__(, ) __VA_ARGS__)

#define _FIXUMOV(op, rd, rn, idx, ...)                                         \
    op(__FORCE(rasReg, rd), __FORCE(rasVReg, rn), __FORCE_IMM(idx))
#define _FIXINS(op, rd, idx1, rn, ...)                                         \
    op(__FORCE(rasVReg, rd), __FORCE_IMM(idx1),                                \
       _Generic(rn,                                                            \
           rasReg: rn,                                                         \
           rasVReg: rn,                                                        \
           default: *(rasReg*) _ras_invalid_argument_type) __VA_OPT__(, )      \
           __VA_ARGS__)

#define MOVS(rd, ...)                                                          \
    _Generic(rd,                                                               \
        rasReg: _FIXUMOV(UMOVSW, rd, __VA_ARGS__),                             \
        rasVReg: _FIXINS(INSS, rd, __VA_ARGS__))
#define MOVD(rd, ...)                                                          \
    _Generic(rd,                                                               \
        rasReg: _FIXUMOV(UMOVD, rd, __VA_ARGS__),                              \
        rasVReg: _FIXINS(INSD, rd, __VA_ARGS__))

#define ADVSIMDSCALARPAIRWISE(sz, u, opcode, rd, rn)                           \
    __EMIT(AdvSIMDScalarPairwise, u, sz, opcode, rn, rd)

#define FADDPS(rd, rn) ADVSIMDSCALARPAIRWISE(0, 1, 13, rd, rn)
#define FADDPD(rd, rn) ADVSIMDSCALARPAIRWISE(1, 1, 13, rd, rn)

#define ADVSIMDSCALAR2MISC(sz, u, opcode, rd, rn)                              \
    __EMIT(AdvSIMDScalar2Misc, u, sz, opcode, rn, rd)

#define FRECPES(rd, rn) ADVSIMDSCALAR2MISC(2, 0, 29, rd, rn)
#define FRECPED(rd, rn) ADVSIMDSCALAR2MISC(3, 0, 29, rd, rn)
#define FRSQRTES(rd, rn) ADVSIMDSCALAR2MISC(2, 1, 29, rd, rn)
#define FRSQRTED(rd, rn) ADVSIMDSCALAR2MISC(3, 1, 29, rd, rn)

#define ADVSIMD2MISC(q, sz, u, opcode, rd, rn)                                 \
    __EMIT(AdvSIMD2Misc, q, u, sz, opcode, rn, rd)

#define FRINTM2S(rd, rn) ADVSIMD2MISC(0, 0, 0, 25, rd, rn)
#define FRINTM4S(rd, rn) ADVSIMD2MISC(1, 0, 0, 25, rd, rn)
#define FRINTM2D(rd, rn) ADVSIMD2MISC(1, 1, 0, 25, rd, rn)
#define FCMEQZ2S(rd, rn) ADVSIMD2MISC(0, 2, 0, 13, rd, rn)
#define FCMEQZ4S(rd, rn) ADVSIMD2MISC(1, 2, 0, 13, rd, rn)
#define FCMEQZ2D(rd, rn) ADVSIMD2MISC(1, 3, 0, 13, rd, rn)
#define FCVTZS2S(rd, rn) ADVSIMD2MISC(0, 2, 0, 27, rd, rn)
#define FCVTZS4S(rd, rn) ADVSIMD2MISC(1, 2, 0, 27, rd, rn)
#define FCVTZS2D(rd, rn) ADVSIMD2MISC(1, 3, 0, 27, rd, rn)
#define FNEG2S(rd, rn) ADVSIMD2MISC(0, 2, 1, 15, rd, rn)
#define FNEG4S(rd, rn) ADVSIMD2MISC(1, 2, 1, 15, rd, rn)
#define FNEG2D(rd, rn) ADVSIMD2MISC(1, 3, 1, 15, rd, rn)

#define ADVSIMD3SAME(q, sz, u, opcode, rd, rn, rm)                             \
    __EMIT(AdvSIMD3Same, q, u, sz, rm, opcode, rn, rd)

#define SHADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 0, rd, rn, rm)
#define SHADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 0, rd, rn, rm)
#define SHADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 0, rd, rn, rm)
#define SHADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 0, rd, rn, rm)
#define SHADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 0, rd, rn, rm)
#define SHADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 0, rd, rn, rm)
#define SQADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 1, rd, rn, rm)
#define SQADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 1, rd, rn, rm)
#define SQADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 1, rd, rn, rm)
#define SQADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 1, rd, rn, rm)
#define SQADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 1, rd, rn, rm)
#define SQADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 1, rd, rn, rm)
#define SRHADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 2, rd, rn, rm)
#define SRHADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 2, rd, rn, rm)
#define SRHADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 2, rd, rn, rm)
#define SRHADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 2, rd, rn, rm)
#define SRHADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 2, rd, rn, rm)
#define SRHADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 2, rd, rn, rm)
#define SHSUB8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 4, rd, rn, rm)
#define SHSUB16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 4, rd, rn, rm)
#define SHSUB4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 4, rd, rn, rm)
#define SHSUB8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 4, rd, rn, rm)
#define SHSUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 4, rd, rn, rm)
#define SHSUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 4, rd, rn, rm)
#define SQSUB8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 5, rd, rn, rm)
#define SQSUB16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 5, rd, rn, rm)
#define SQSUB4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 5, rd, rn, rm)
#define SQSUB8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 5, rd, rn, rm)
#define SQSUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 5, rd, rn, rm)
#define SQSUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 5, rd, rn, rm)
#define CMGT8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 6, rd, rn, rm)
#define CMGT16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 6, rd, rn, rm)
#define CMGT4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 6, rd, rn, rm)
#define CMGT8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 6, rd, rn, rm)
#define CMGT2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 6, rd, rn, rm)
#define CMGT4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 6, rd, rn, rm)
#define CMGE8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 7, rd, rn, rm)
#define CMGE16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 7, rd, rn, rm)
#define CMGE4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 7, rd, rn, rm)
#define CMGE8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 7, rd, rn, rm)
#define CMGE2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 7, rd, rn, rm)
#define CMGE4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 7, rd, rn, rm)
#define SSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 8, rd, rn, rm)
#define SSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 8, rd, rn, rm)
#define SSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 8, rd, rn, rm)
#define SSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 8, rd, rn, rm)
#define SSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 8, rd, rn, rm)
#define SSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 8, rd, rn, rm)
#define SQSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 9, rd, rn, rm)
#define SQSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 9, rd, rn, rm)
#define SQSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 9, rd, rn, rm)
#define SQSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 9, rd, rn, rm)
#define SQSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 9, rd, rn, rm)
#define SQSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 9, rd, rn, rm)
#define SRSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 10, rd, rn, rm)
#define SRSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 10, rd, rn, rm)
#define SRSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 10, rd, rn, rm)
#define SRSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 10, rd, rn, rm)
#define SRSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 10, rd, rn, rm)
#define SRSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 10, rd, rn, rm)
#define SQRSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 11, rd, rn, rm)
#define SQRSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 11, rd, rn, rm)
#define SQRSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 11, rd, rn, rm)
#define SQRSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 11, rd, rn, rm)
#define SQRSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 11, rd, rn, rm)
#define SQRSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 11, rd, rn, rm)
#define SMAX8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 12, rd, rn, rm)
#define SMAX16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 12, rd, rn, rm)
#define SMAX4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 12, rd, rn, rm)
#define SMAX8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 12, rd, rn, rm)
#define SMAX2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 12, rd, rn, rm)
#define SMAX4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 12, rd, rn, rm)
#define SMIN8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 13, rd, rn, rm)
#define SMIN16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 13, rd, rn, rm)
#define SMIN4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 13, rd, rn, rm)
#define SMIN8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 13, rd, rn, rm)
#define SMIN2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 13, rd, rn, rm)
#define SMIN4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 13, rd, rn, rm)
#define SABD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 14, rd, rn, rm)
#define SABD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 14, rd, rn, rm)
#define SABD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 14, rd, rn, rm)
#define SABD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 14, rd, rn, rm)
#define SABD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 14, rd, rn, rm)
#define SABD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 14, rd, rn, rm)
#define SABA8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 15, rd, rn, rm)
#define SABA16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 15, rd, rn, rm)
#define SABA4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 15, rd, rn, rm)
#define SABA8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 15, rd, rn, rm)
#define SABA2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 15, rd, rn, rm)
#define SABA4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 15, rd, rn, rm)
#define ADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 16, rd, rn, rm)
#define ADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 16, rd, rn, rm)
#define ADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 16, rd, rn, rm)
#define ADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 16, rd, rn, rm)
#define ADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 16, rd, rn, rm)
#define ADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 16, rd, rn, rm)
#define CMTST8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 17, rd, rn, rm)
#define CMTST16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 17, rd, rn, rm)
#define CMTST4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 17, rd, rn, rm)
#define CMTST8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 17, rd, rn, rm)
#define CMTST2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 17, rd, rn, rm)
#define CMTST4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 17, rd, rn, rm)
#define MLA8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 18, rd, rn, rm)
#define MLA16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 18, rd, rn, rm)
#define MLA4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 18, rd, rn, rm)
#define MLA8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 18, rd, rn, rm)
#define MLA2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 18, rd, rn, rm)
#define MLA4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 18, rd, rn, rm)
#define MUL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 19, rd, rn, rm)
#define MUL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 19, rd, rn, rm)
#define MUL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 19, rd, rn, rm)
#define MUL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 19, rd, rn, rm)
#define MUL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 19, rd, rn, rm)
#define MUL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 19, rd, rn, rm)
#define SMAXP8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 20, rd, rn, rm)
#define SMAXP16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 20, rd, rn, rm)
#define SMAXP4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 20, rd, rn, rm)
#define SMAXP8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 20, rd, rn, rm)
#define SMAXP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 20, rd, rn, rm)
#define SMAXP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 20, rd, rn, rm)
#define SMINP8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 21, rd, rn, rm)
#define SMINP16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 21, rd, rn, rm)
#define SMINP4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 21, rd, rn, rm)
#define SMINP8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 21, rd, rn, rm)
#define SMINP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 21, rd, rn, rm)
#define SMINP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 21, rd, rn, rm)
#define SQDMULH4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 22, rd, rn, rm)
#define SQDMULH8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 22, rd, rn, rm)
#define SQDMULH2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 22, rd, rn, rm)
#define SQDMULH4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 22, rd, rn, rm)
#define ADDP8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 23, rd, rn, rm)
#define ADDP16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 23, rd, rn, rm)
#define ADDP4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 23, rd, rn, rm)
#define ADDP8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 23, rd, rn, rm)
#define ADDP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 23, rd, rn, rm)
#define ADDP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 23, rd, rn, rm)
#define UHADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 0, rd, rn, rm)
#define UHADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 0, rd, rn, rm)
#define UHADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 0, rd, rn, rm)
#define UHADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 0, rd, rn, rm)
#define UHADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 0, rd, rn, rm)
#define UHADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 0, rd, rn, rm)
#define UQADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 1, rd, rn, rm)
#define UQADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 1, rd, rn, rm)
#define UQADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 1, rd, rn, rm)
#define UQADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 1, rd, rn, rm)
#define UQADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 1, rd, rn, rm)
#define UQADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 1, rd, rn, rm)
#define URHADD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 2, rd, rn, rm)
#define URHADD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 2, rd, rn, rm)
#define URHADD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 2, rd, rn, rm)
#define URHADD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 2, rd, rn, rm)
#define URHADD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 2, rd, rn, rm)
#define URHADD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 2, rd, rn, rm)
#define UHSUB8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 4, rd, rn, rm)
#define UHSUB16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 4, rd, rn, rm)
#define UHSUB4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 4, rd, rn, rm)
#define UHSUB8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 4, rd, rn, rm)
#define UHSUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 4, rd, rn, rm)
#define UHSUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 4, rd, rn, rm)
#define UQSUB8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 5, rd, rn, rm)
#define UQSUB16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 5, rd, rn, rm)
#define UQSUB4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 5, rd, rn, rm)
#define UQSUB8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 5, rd, rn, rm)
#define UQSUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 5, rd, rn, rm)
#define UQSUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 5, rd, rn, rm)
#define CMHI8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 6, rd, rn, rm)
#define CMHI16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 6, rd, rn, rm)
#define CMHI4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 6, rd, rn, rm)
#define CMHI8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 6, rd, rn, rm)
#define CMHI2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 6, rd, rn, rm)
#define CMHI4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 6, rd, rn, rm)
#define CMHS8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 7, rd, rn, rm)
#define CMHS16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 7, rd, rn, rm)
#define CMHS4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 7, rd, rn, rm)
#define CMHS8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 7, rd, rn, rm)
#define CMHS2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 7, rd, rn, rm)
#define CMHS4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 7, rd, rn, rm)
#define USHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 8, rd, rn, rm)
#define USHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 8, rd, rn, rm)
#define USHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 8, rd, rn, rm)
#define USHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 8, rd, rn, rm)
#define USHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 8, rd, rn, rm)
#define USHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 8, rd, rn, rm)
#define UQSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 9, rd, rn, rm)
#define UQSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 9, rd, rn, rm)
#define UQSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 9, rd, rn, rm)
#define UQSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 9, rd, rn, rm)
#define UQSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 9, rd, rn, rm)
#define UQSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 9, rd, rn, rm)
#define URSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 10, rd, rn, rm)
#define URSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 10, rd, rn, rm)
#define URSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 10, rd, rn, rm)
#define URSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 10, rd, rn, rm)
#define URSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 10, rd, rn, rm)
#define URSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 10, rd, rn, rm)
#define UQRSHL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 11, rd, rn, rm)
#define UQRSHL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 11, rd, rn, rm)
#define UQRSHL4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 11, rd, rn, rm)
#define UQRSHL8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 11, rd, rn, rm)
#define UQRSHL2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 11, rd, rn, rm)
#define UQRSHL4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 11, rd, rn, rm)
#define UMAX8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 12, rd, rn, rm)
#define UMAX16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 12, rd, rn, rm)
#define UMAX4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 12, rd, rn, rm)
#define UMAX8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 12, rd, rn, rm)
#define UMAX2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 12, rd, rn, rm)
#define UMAX4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 12, rd, rn, rm)
#define UMIN8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 13, rd, rn, rm)
#define UMIN16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 13, rd, rn, rm)
#define UMIN4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 13, rd, rn, rm)
#define UMIN8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 13, rd, rn, rm)
#define UMIN2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 13, rd, rn, rm)
#define UMIN4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 13, rd, rn, rm)
#define UABD8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 14, rd, rn, rm)
#define UABD16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 14, rd, rn, rm)
#define UABD4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 14, rd, rn, rm)
#define UABD8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 14, rd, rn, rm)
#define UABD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 14, rd, rn, rm)
#define UABD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 14, rd, rn, rm)
#define UABA8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 15, rd, rn, rm)
#define UABA16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 15, rd, rn, rm)
#define UABA4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 15, rd, rn, rm)
#define UABA8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 15, rd, rn, rm)
#define UABA2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 15, rd, rn, rm)
#define UABA4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 15, rd, rn, rm)
#define SUB8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 16, rd, rn, rm)
#define SUB16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 16, rd, rn, rm)
#define SUB4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 16, rd, rn, rm)
#define SUB8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 16, rd, rn, rm)
#define SUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 16, rd, rn, rm)
#define SUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 16, rd, rn, rm)
#define CMEQ8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 17, rd, rn, rm)
#define CMEQ16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 17, rd, rn, rm)
#define CMEQ4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 17, rd, rn, rm)
#define CMEQ8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 17, rd, rn, rm)
#define CMEQ2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 17, rd, rn, rm)
#define CMEQ4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 17, rd, rn, rm)
#define MLS8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 18, rd, rn, rm)
#define MLS16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 18, rd, rn, rm)
#define MLS4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 18, rd, rn, rm)
#define MLS8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 18, rd, rn, rm)
#define MLS2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 18, rd, rn, rm)
#define MLS4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 18, rd, rn, rm)
#define PMUL8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 19, rd, rn, rm)
#define PMUL16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 19, rd, rn, rm)
#define UMAXP8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 20, rd, rn, rm)
#define UMAXP16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 20, rd, rn, rm)
#define UMAXP4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 20, rd, rn, rm)
#define UMAXP8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 20, rd, rn, rm)
#define UMAXP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 20, rd, rn, rm)
#define UMAXP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 20, rd, rn, rm)
#define UMINP8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 21, rd, rn, rm)
#define UMINP16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 21, rd, rn, rm)
#define UMINP4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 21, rd, rn, rm)
#define UMINP8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 21, rd, rn, rm)
#define UMINP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 21, rd, rn, rm)
#define UMINP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 21, rd, rn, rm)
#define SQRDMULH4H(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 22, rd, rn, rm)
#define SQRDMULH8H(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 22, rd, rn, rm)
#define SQRDMULH2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 22, rd, rn, rm)
#define SQRDMULH4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 22, rd, rn, rm)
#define FMAXNM2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 24, rd, rn, rm)
#define FMAXNM4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 24, rd, rn, rm)
#define FMAXNM2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 24, rd, rn, rm)
#define FMLA2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 25, rd, rn, rm)
#define FMLA4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 25, rd, rn, rm)
#define FMLA2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 25, rd, rn, rm)
#define FADD2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 26, rd, rn, rm)
#define FADD4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 26, rd, rn, rm)
#define FADD2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 26, rd, rn, rm)
#define FMULX2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 27, rd, rn, rm)
#define FMULX4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 27, rd, rn, rm)
#define FMULX2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 27, rd, rn, rm)
#define FCMEQ2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 28, rd, rn, rm)
#define FCMEQ4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 28, rd, rn, rm)
#define FCMEQ2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 28, rd, rn, rm)
#define FMAX2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 30, rd, rn, rm)
#define FMAX4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 30, rd, rn, rm)
#define FMAX2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 30, rd, rn, rm)
#define FRECPS2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 31, rd, rn, rm)
#define FRECPS4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 31, rd, rn, rm)
#define FRECPS2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 31, rd, rn, rm)
#define FMINNM2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 24, rd, rn, rm)
#define FMINNM4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 24, rd, rn, rm)
#define FMINNM2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 24, rd, rn, rm)
#define FMLS2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 25, rd, rn, rm)
#define FMLS4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 25, rd, rn, rm)
#define FMLS2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 25, rd, rn, rm)
#define FSUB2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 26, rd, rn, rm)
#define FSUB4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 26, rd, rn, rm)
#define FSUB2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 26, rd, rn, rm)
#define FMIN2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 30, rd, rn, rm)
#define FMIN4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 30, rd, rn, rm)
#define FMIN2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 30, rd, rn, rm)
#define FRSQRTS2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 31, rd, rn, rm)
#define FRSQRTS4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 31, rd, rn, rm)
#define FRSQRTS2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 31, rd, rn, rm)
#define FMAXNMP2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 24, rd, rn, rm)
#define FMAXNMP4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 24, rd, rn, rm)
#define FMAXNMP2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 24, rd, rn, rm)
#define FADDP2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 26, rd, rn, rm)
#define FADDP4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 26, rd, rn, rm)
#define FADDP2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 26, rd, rn, rm)
#define FMUL2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 27, rd, rn, rm)
#define FMUL4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 27, rd, rn, rm)
#define FMUL2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 27, rd, rn, rm)
#define FCMGE2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 28, rd, rn, rm)
#define FCMGE4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 28, rd, rn, rm)
#define FCMGE2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 28, rd, rn, rm)
#define FACGE2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 29, rd, rn, rm)
#define FACGE4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 29, rd, rn, rm)
#define FACGE2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 29, rd, rn, rm)
#define FMAXP2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 30, rd, rn, rm)
#define FMAXP4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 30, rd, rn, rm)
#define FMAXP2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 30, rd, rn, rm)
#define FDIV2S(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 31, rd, rn, rm)
#define FDIV4S(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 31, rd, rn, rm)
#define FDIV2D(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 31, rd, rn, rm)
#define FMINNMP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 24, rd, rn, rm)
#define FMINNMP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 24, rd, rn, rm)
#define FMINNMP2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 24, rd, rn, rm)
#define FABD2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 26, rd, rn, rm)
#define FABD4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 26, rd, rn, rm)
#define FABD2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 26, rd, rn, rm)
#define FCMGT2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 28, rd, rn, rm)
#define FCMGT4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 28, rd, rn, rm)
#define FCMGT2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 28, rd, rn, rm)
#define FACGT2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 29, rd, rn, rm)
#define FACGT4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 29, rd, rn, rm)
#define FACGT2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 29, rd, rn, rm)
#define FMINP2S(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 30, rd, rn, rm)
#define FMINP4S(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 30, rd, rn, rm)
#define FMINP2D(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 30, rd, rn, rm)
#define AND8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 0, 3, rd, rn, rm)
#define AND16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 0, 3, rd, rn, rm)
#define BIC8B(rd, rn, rm) ADVSIMD3SAME(0, 1, 0, 3, rd, rn, rm)
#define BIC16B(rd, rn, rm) ADVSIMD3SAME(1, 1, 0, 3, rd, rn, rm)
#define ORR8B(rd, rn, rm) ADVSIMD3SAME(0, 2, 0, 3, rd, rn, rm)
#define ORR16B(rd, rn, rm) ADVSIMD3SAME(1, 2, 0, 3, rd, rn, rm)
#define ORN8B(rd, rn, rm) ADVSIMD3SAME(0, 3, 0, 3, rd, rn, rm)
#define ORN16B(rd, rn, rm) ADVSIMD3SAME(1, 3, 0, 3, rd, rn, rm)
#define EOR8B(rd, rn, rm) ADVSIMD3SAME(0, 0, 1, 3, rd, rn, rm)
#define EOR16B(rd, rn, rm) ADVSIMD3SAME(1, 0, 1, 3, rd, rn, rm)
#define BSL8B(rd, rn, rm) ADVSIMD3SAME(0, 1, 1, 3, rd, rn, rm)
#define BSL16B(rd, rn, rm) ADVSIMD3SAME(1, 1, 1, 3, rd, rn, rm)
#define BIT8B(rd, rn, rm) ADVSIMD3SAME(0, 2, 1, 3, rd, rn, rm)
#define BIT16B(rd, rn, rm) ADVSIMD3SAME(1, 2, 1, 3, rd, rn, rm)
#define BIF8B(rd, rn, rm) ADVSIMD3SAME(0, 3, 1, 3, rd, rn, rm)
#define BIF16B(rd, rn, rm) ADVSIMD3SAME(1, 3, 1, 3, rd, rn, rm)
#define MOV8B(rd, rn) ORR8B(rd, rn, rn)
#define MOV16B(rd, rn) ORR16B(rd, rn, rn)

#define ADVSIMDMODIMMFLOAT(q, op, cmode, o2, rd, fimm)                         \
    __EMIT(AdvSIMDModImmFloat, q, op, cmode, o2, fimm, rd)

#define FMOV2S(rd, fimm) ADVSIMDMODIMMFLOAT(0, 0, 15, 0, rd, fimm)
#define FMOV4S(rd, fimm) ADVSIMDMODIMMFLOAT(1, 0, 15, 0, rd, fimm)
#define FMOV2D(rd, fimm) ADVSIMDMODIMMFLOAT(1, 1, 15, 0, rd, fimm)

#define REG(n) ((rasReg) {n})

#define R0 REG(0)
#define R1 REG(1)
#define R2 REG(2)
#define R3 REG(3)
#define R4 REG(4)
#define R5 REG(5)
#define R6 REG(6)
#define R7 REG(7)
#define R8 REG(8)
#define R9 REG(9)
#define R10 REG(10)
#define R11 REG(11)
#define R12 REG(12)
#define R13 REG(13)
#define R14 REG(14)
#define R15 REG(15)
#define R16 REG(16)
#define R17 REG(17)
#define R18 REG(18)
#define R19 REG(19)
#define R20 REG(20)
#define R21 REG(21)
#define R22 REG(22)
#define R23 REG(23)
#define R24 REG(24)
#define R25 REG(25)
#define R26 REG(26)
#define R27 REG(27)
#define R28 REG(28)
#define R29 REG(29)
#define R30 REG(30)
#define XR R8
#define IP0 R16
#define IP1 R17
#define FP R29
#define LR R30
#define ZR ((rasReg) {31, 0})
#define SP ((rasReg) {31, 1})

#define VREG(n) ((rasVReg) {n})

#define V0 VREG(0)
#define V1 VREG(1)
#define V2 VREG(2)
#define V3 VREG(3)
#define V4 VREG(4)
#define V5 VREG(5)
#define V6 VREG(6)
#define V7 VREG(7)
#define V8 VREG(8)
#define V9 VREG(9)
#define V10 VREG(10)
#define V11 VREG(11)
#define V12 VREG(12)
#define V13 VREG(13)
#define V14 VREG(14)
#define V15 VREG(15)
#define V16 VREG(16)
#define V17 VREG(17)
#define V18 VREG(18)
#define V19 VREG(19)
#define V20 VREG(20)
#define V21 VREG(21)
#define V22 VREG(22)
#define V23 VREG(23)
#define V24 VREG(24)
#define V25 VREG(25)
#define V26 VREG(26)
#define V27 VREG(27)
#define V28 VREG(28)
#define V29 VREG(29)
#define V30 VREG(30)
#define V31 VREG(31)

#define NZCV 0xda10

#ifdef RAS_DEFAULT_SUFFIX
#define __CAT(x, y) ___CAT(x, y)
#define ___CAT(x, y) x##y
#define _(x) __CAT(x, RAS_DEFAULT_SUFFIX)

#define ADD _(ADD)
#define SUB _(SUB)
#define ADDS _(ADDS)
#define SUBS _(SUBS)
#define CMP _(CMP)
#define CMN _(CMN)
#define NEG _(NEG)
#define NEGS _(NEGS)
#define ADC _(ADC)
#define SBC _(SBC)
#define ADCS _(ADCS)
#define SBCS _(SBCS)
#define NGC _(NGC)
#define NGCS _(NGCS)
#define AND _(AND)
#define BIC _(BIC)
#define ORR _(ORR)
#define ORN _(ORN)
#define EOR _(EOR)
#define EON _(EON)
#define ANDS _(ANDS)
#define BICS _(BICS)
#define MVN _(MVN)
#define TST _(TST)
#define RBIT _(RBIT)
#define REV _(REV)
#define REV16 REV16W
#define CLZ _(CLZ)
#define CLS _(CLS)
#define UDIV _(UDIV)
#define SDIV _(SDIV)
#define LSLV _(LSLV)
#define LSRV _(LSRV)
#define ASRV _(ASRV)
#define RORV _(RORV)
#define MADD _(MADD)
#define MSUB _(MSUB)
#define MUL _(MUL)
#define MNEG _(MNEG)
#define CSEL _(CSEL)
#define CSINC _(CSINC)
#define CSINV _(CSINV)
#define CSNEG _(CSNEG)
#define CMOV _(CMOV)
#define CSET _(CSET)
#define CSETM _(CSETM)
#define CINC _(CINC)
#define CINV _(CINV)
#define CNEG _(CNEG)
#define SBFM _(SBFM)
#define BFM _(BFM)
#define UBFM _(UBFM)
#define SBFIZ _(SBFIZ)
#define SBFX _(SBFX)
#define BFI _(BFI)
#define BFC _(BFC)
#define BFXIL _(BFXIL)
#define UBFIZ _(UBFIZ)
#define UBFX _(UBFX)
#define EXTR _(EXTR)
#define LSL_ _(LSL)
#define LSR_ _(LSR)
#define ASR_ _(ASR)
#define ROR _(ROR)
#define SXTB_ _(SXTB)
#define SXTH_ _(SXTH)
#define MOVN _(MOVN)
#define MOVZ _(MOVZ)
#define MOVK _(MOVK)
#define MOV _(MOV)
#define LDRSB _(LDRSB)
#define LDRSH _(LDRSH)
#define STR _(STR)
#define LDR _(LDR)
#define LDRL _(LDRL)
#define STP _(STP)
#define LDP _(LDP)
#define CBZ _(CBZ)
#define CBNZ _(CBNZ)

#define SCVTFS _(SCVTFS)
#define UCVTFS _(UCVTFS)
#define SCVTFD _(SCVTFD)
#define UCVTFD _(UCVTFD)
#define FCVTZSS _(FCVTZSS)
#define FCVTZUS _(FCVTZUS)
#define FCVTZSD _(FCVTZSD)
#define FCVTZUD _(FCVTZUD)

#define UMOVB _(UMOVB)
#define SMOVB _(SMOVB)
#define UMOVH _(UMOVH)
#define SMOVH _(SMOVH)
#define UMOVS _(UMOVS)
#define SMOVS _(SMOVS)

#endif

#endif