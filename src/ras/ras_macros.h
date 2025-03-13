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
#define __FORCE_INT(op)                                                        \
    _Generic(op,                                                               \
        rasReg: *(int*) _ras_invalid_argument_type,                            \
        rasLabel: *(int*) _ras_invalid_argument_type,                          \
        default: op)
#define __FORCE_FLT(op)                                                        \
    _Generic(op, rasVReg: *(int*) _ras_invalid_argument_type, default: op)
#define __FORCE(type, val)                                                     \
    _Generic(val, type: val, default: *(type*) _ras_invalid_argument_type)

#define align(a) rasAlign(RAS_CTX_VAR, a)

#define word(w) __EMIT(Word, w)
#define dword(d)                                                               \
    _Generic(d,                                                                \
        rasLabel: __EMIT(AbsAddr, __FORCE(rasLabel, d)),                       \
        default: __EMIT(Dword, __FORCE_INT(d)))

#define addsub(sf, op, s, rd, rn, op2, ...)                                    \
    _addsub(sf, op, s, rd, rn, op2, __VA_DFL(lsl(0), __VA_ARGS__))
#define _addsub(sf, op, s, rd, rn, op2, mod)                                   \
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
                           __FORCE_INT(op2), __FORCE(rasReg, mod)),            \
            default: __EMIT(AddSubImm, sf, op, s, __FORCE(rasShift, mod),      \
                            __FORCE_INT(op2), rn, rd)))

#define addw(rd, rn, op2, ...) addsub(0, 0, 0, rd, rn, op2, __VA_ARGS__)
#define addsw(rd, rn, op2, ...) addsub(0, 0, 1, rd, rn, op2, __VA_ARGS__)
#define subw(rd, rn, op2, ...) addsub(0, 1, 0, rd, rn, op2, __VA_ARGS__)
#define subsw(rd, rn, op2, ...) addsub(0, 1, 1, rd, rn, op2, __VA_ARGS__)
#define addx(rd, rn, op2, ...) addsub(1, 0, 0, rd, rn, op2, __VA_ARGS__)
#define addsx(rd, rn, op2, ...) addsub(1, 0, 1, rd, rn, op2, __VA_ARGS__)
#define subx(rd, rn, op2, ...) addsub(1, 1, 0, rd, rn, op2, __VA_ARGS__)
#define subsx(rd, rn, op2, ...) addsub(1, 1, 1, rd, rn, op2, __VA_ARGS__)
#define cmpw(rn, op2, ...) subs(zr, rn, op2, __VA_ARGS__)
#define cmnw(rn, op2, ...) adds(zr, rn, op2, __VA_ARGS__)
#define cmpx(rn, op2, ...) subsx(zr, rn, op2, __VA_ARGS__)
#define cmnx(rn, op2, ...) addsx(zr, rn, op2, __VA_ARGS__)

#define addsubcarry(sf, op, s, rd, rn, rm)                                     \
    __EMIT(AddSubCarry, sf, op, s, rm, rn, rd)

#define adcw(rd, rn, rm) addsubcarry(0, 0, 0, rd, rn, rm)
#define sbcw(rd, rn, rm) addsubcarry(0, 1, 0, rd, rn, rm)
#define adcsw(rd, rn, rm) addsubcarry(0, 0, 1, rd, rn, rm)
#define sbcsw(rd, rn, rm) addsubcarry(0, 1, 1, rd, rn, rm)
#define adcx(rd, rn, rm) addsubcarry(1, 0, 0, rd, rn, rm)
#define sbcx(rd, rn, rm) addsubcarry(1, 1, 0, rd, rn, rm)
#define adcsx(rd, rn, rm) addsubcarry(1, 0, 1, rd, rn, rm)
#define sbcsx(rd, rn, rm) addsubcarry(1, 1, 1, rd, rn, rm)

#define __CINV(n, v) ((n) ? ~(v) : (v))

#define logical(sf, opc, n, rd, rn, op2, ...)                                  \
    _logical(sf, opc, n, rd, rn, op2, __VA_DFL(lsl(0), __VA_ARGS__))
#define _logical(sf, opc, n, rd, rn, op2, mod)                                 \
    _Generic(op2,                                                              \
        rasReg: __EMIT(LogicalReg, sf, opc, n, __FORCE(rasShift, mod),         \
                       __FORCE(rasReg, op2), rn, rd),                          \
        default: _Generic(mod,                                                 \
            rasReg: __EMIT(PseudoLogicalImm, sf, opc, rd, rn,                  \
                           __CINV(n, __FORCE_INT(op2)), __FORCE(rasReg, mod)), \
            default: __EMIT(LogicalImm, sf, opc, __CINV(n, __FORCE_INT(op2)),  \
                            rn, rd)))

#define andw(rd, rn, op2, ...) logical(0, 0, 0, rd, rn, op2, __VA_ARGS__)
#define bicw(rd, rn, op2, ...) logical(0, 0, 1, rd, rn, op2, __VA_ARGS__)
#define orrw(rd, rn, op2, ...) logical(0, 1, 0, rd, rn, op2, __VA_ARGS__)
#define ornw(rd, rn, op2, ...) logical(0, 1, 1, rd, rn, op2, __VA_ARGS__)
#define eorw(rd, rn, op2, ...) logical(0, 2, 0, rd, rn, op2, __VA_ARGS__)
#define eonw(rd, rn, op2, ...) logical(0, 2, 1, rd, rn, op2, __VA_ARGS__)
#define andsw(rd, rn, op2, ...) logical(0, 3, 0, rd, rn, op2, __VA_ARGS__)
#define bicsw(rd, rn, op2, ...) logical(0, 3, 1, rd, rn, op2, __VA_ARGS__)
#define andx(rd, rn, op2, ...) logical(1, 0, 0, rd, rn, op2, __VA_ARGS__)
#define bicx(rd, rn, op2, ...) logical(1, 0, 1, rd, rn, op2, __VA_ARGS__)
#define orrx(rd, rn, op2, ...) logical(1, 1, 0, rd, rn, op2, __VA_ARGS__)
#define ornx(rd, rn, op2, ...) logical(1, 1, 1, rd, rn, op2, __VA_ARGS__)
#define eorx(rd, rn, op2, ...) logical(1, 2, 0, rd, rn, op2, __VA_ARGS__)
#define eonx(rd, rn, op2, ...) logical(1, 2, 1, rd, rn, op2, __VA_ARGS__)
#define andsx(rd, rn, op2, ...) logical(1, 3, 0, rd, rn, op2, __VA_ARGS__)
#define bicsx(rd, rn, op2, ...) logical(1, 3, 1, rd, rn, op2, __VA_ARGS__)
#define mvnw(rn, rm) ornw(rn, zr, rm)
#define tstw(rn, op2, ...) andsw(zr, rn, op2, __VA_ARGS__)
#define mvnx(rn, rm) ornx(rn, zr, rm)
#define tstx(rn, op2, ...) andsx(zr, rn, op2, __VA_ARGS__)

#define dataproc1source(sf, s, opcode2, opcode, rd, rn)                        \
    __EMIT(DataProc1Source, sf, s, opcode2, opcode, rn, rd)

#define rbitw(rd, rn) dataproc1source(0, 0, 0, 0, rd, rn)
#define rev16w(rd, rn) dataproc1source(0, 0, 0, 1, rd, rn)
#define revw(rd, rn) dataproc1source(0, 0, 0, 2, rd, rn)
#define clzw(rd, rn) dataproc1source(0, 0, 0, 4, rd, rn)
#define clsw(rd, rn) dataproc1source(0, 0, 0, 5, rd, rn)
#define rbitx(rd, rn) dataproc1source(1, 0, 0, 0, rd, rn)
#define rev16x(rd, rn) dataproc1source(1, 0, 0, 1, rd, rn)
#define rev32(rd, rn) dataproc1source(1, 0, 0, 2, rd, rn)
#define revx(rd, rn) dataproc1source(1, 0, 0, 3, rd, rn)
#define clzx(rd, rn) dataproc1source(1, 0, 0, 4, rd, rn)
#define clsx(rd, rn) dataproc1source(1, 0, 0, 5, rd, rn)

#define dataproc2source(sf, s, opcode, rd, rn, rm)                             \
    __EMIT(DataProc2Source, sf, s, rm, opcode, rn, rd)

#define udivw(rd, rn, rm) dataproc2source(0, 0, 2, rd, rn, rm)
#define sdivw(rd, rn, rm) dataproc2source(0, 0, 3, rd, rn, rm)
#define lslvw(rd, rn, rm) dataproc2source(0, 0, 8, rd, rn, rm)
#define lsrvw(rd, rn, rm) dataproc2source(0, 0, 9, rd, rn, rm)
#define asrvw(rd, rn, rm) dataproc2source(0, 0, 10, rd, rn, rm)
#define rorvw(rd, rn, rm) dataproc2source(0, 0, 11, rd, rn, rm)
#define udivx(rd, rn, rm) dataproc2source(1, 0, 2, rd, rn, rm)
#define sdivx(rd, rn, rm) dataproc2source(1, 0, 3, rd, rn, rm)
#define lslvx(rd, rn, rm) dataproc2source(1, 0, 8, rd, rn, rm)
#define lsrvx(rd, rn, rm) dataproc2source(1, 0, 9, rd, rn, rm)
#define asrvx(rd, rn, rm) dataproc2source(1, 0, 10, rd, rn, rm)
#define rorvx(rd, rn, rm) dataproc2source(1, 0, 11, rd, rn, rm)

#define dataproc3source(sf, op54, op31, o0, rd, rn, rm, ra)                    \
    __EMIT(DataProc3Source, sf, op54, op31, rm, o0, ra, rn, rd)

#define maddw(rd, rn, rm, ra) dataproc3source(0, 0, 0, 0, rd, rn, rm, ra)
#define msubw(rd, rn, rm, ra) dataproc3source(0, 0, 0, 1, rd, rn, rm, ra)
#define maddx(rd, rn, rm, ra) dataproc3source(1, 0, 0, 0, rd, rn, rm, ra)
#define msubx(rd, rn, rm, ra) dataproc3source(1, 0, 0, 1, rd, rn, rm, ra)
#define smaddl(rd, rn, rm, ra) dataproc3source(1, 0, 1, 0, rd, rn, rm, ra)
#define umaddl(rd, rn, rm, ra) dataproc3source(1, 0, 5, 0, rd, rn, rm, ra)
#define mulw(rd, rn, rm) maddw(rd, rn, rm, zr)
#define mnegw(rd, rn, rm) msubw(rd, rn, rm, zr)
#define mulx(rd, rn, rm) maddx(rd, rn, rm, zr)
#define mnegx(rd, rn, rm) msubx(rd, rn, rm, zr)
#define smull(rd, rn, rm) smaddl(rd, rn, rm, zr)
#define umull(rd, rn, rm) umaddl(rd, rn, rm, zr)

#define condselect(sf, op, s, op2, rd, rn, rm, cond)                           \
    __EMIT(CondSelect, sf, op, s, rm, cond, op2, rn, rd)

#define cselw(rd, rn, rm, cond) condselect(0, 0, 0, 0, rd, rn, rm, cond)
#define csincw(rd, rn, rm, cond) condselect(0, 0, 0, 1, rd, rn, rm, cond)
#define csinvw(rd, rn, rm, cond) condselect(0, 1, 0, 0, rd, rn, rm, cond)
#define csnegw(rd, rn, rm, cond) condselect(0, 1, 0, 1, rd, rn, rm, cond)
#define cselx(rd, rn, rm, cond) condselect(1, 0, 0, 0, rd, rn, rm, cond)
#define csincx(rd, rn, rm, cond) condselect(1, 0, 0, 1, rd, rn, rm, cond)
#define csinvx(rd, rn, rm, cond) condselect(1, 1, 0, 0, rd, rn, rm, cond)
#define csnegx(rd, rn, rm, cond) condselect(1, 1, 0, 1, rd, rn, rm, cond)
#define cmovw(rd, rm, cond) cselw(rd, rm, rd, cond)
#define csetw(rd, cond) csincw(rd, zr, zr, (cond) ^ 1)
#define csetmw(rd, cond) csinvw(rd, zr, zr, (cond) ^ 1)
#define cinvw(rd, rm, cond) csinvw(rd, rm, rm, (cond) ^ 1)
#define cincw(rd, rm, cond) csincw(rd, rm, rm, (cond) ^ 1)
#define cnegw(rd, rm, cond) csnegw(rd, rm, rm, (cond) ^ 1)
#define cmovx(rd, rm, cond) cselx(rd, rm, rd, (cond) ^ 1)
#define csetx(rd, cond) csincx(rd, zr, zr, (cond) ^ 1)
#define csetmx(rd, cond) csinvx(rd, zr, zr, (cond) ^ 1)
#define cinvx(rd, rm, cond) csinvx(rd, rm, rm, (cond) ^ 1)
#define cincx(rd, rm, cond) csincx(rd, rm, rm, (cond) ^ 1)
#define cnegx(rd, rm, cond) csnegx(rd, rm, rm, (cond) ^ 1)

#define bitfield(sf, opc, n, rd, rn, immr, imms)                               \
    __EMIT(Bitfield, sf, opc, n, immr, imms, rn, rd)

#define sbfmw(rd, rn, immr, imms) bitfield(0, 0, 0, rd, rn, immr, imms)
#define bfmw(rd, rn, immr, imms) bitfield(0, 1, 0, rd, rn, immr, imms)
#define ubfmw(rd, rn, immr, imms) bitfield(0, 2, 0, rd, rn, immr, imms)
#define sbfmx(rd, rn, immr, imms) bitfield(1, 0, 1, rd, rn, immr, imms)
#define bfmx(rd, rn, immr, imms) bitfield(1, 1, 1, rd, rn, immr, imms)
#define ubfmx(rd, rn, immr, imms) bitfield(1, 2, 1, rd, rn, immr, imms)
#define sbfizw(rd, rn, lsb, width) sbfmw(rd, rn, 32 - (lsb), (width) - 1)
#define sbfxw(rd, rn, lsb, width) sbfmw(rd, rn, lsb, (lsb) + (width) - 1)
#define bfiw(rd, rn, lsb, width) bfmw(rd, rn, 32 - (lsb), (width) - 1)
#define bfcw(rd, lsb, width) bfiw(rd, zr, lsb, width)
#define bfxilw(rd, rn, lsb, width) bfmw(rd, rn, lsb, (lsb) + (width) - 1)
#define ubfizw(rd, rn, lsb, width) ubfmw(rd, rn, 32 - (lsb), (width) - 1)
#define ubfxw(rd, rn, lsb, width) ubfmw(rd, rn, lsb, (lsb) + (width) - 1)
#define sbfizx(rd, rn, lsb, width) sbfmx(rd, rn, 64 - (lsb), (width) - 1)
#define sbfxx(rd, rn, lsb, width) sbfmx(rd, rn, lsb, (lsb) + (width) - 1)
#define bfix(rd, rn, lsb, width) bfmx(rd, rn, 64 - (lsb), (width) - 1)
#define bfcx(rd, lsb, width) bfix(rd, zr, lsb, width)
#define bfxilx(rd, rn, lsb, width) bfmx(rd, rn, lsb, (lsb) + (width) - 1)
#define ubfizx(rd, rn, lsb, width) ubfmx(rd, rn, 64 - (lsb), (width) - 1)
#define ubfxx(rd, rn, lsb, width) ubfmx(rd, rn, lsb, (lsb) + (width) - 1)

#define extract(sf, op21, n, o0, rd, rn, rm, imms)                             \
    __EMIT(Extract, sf, op21, n, o0, rm, imms, rn, rd)

#define extrw(rd, rn, rm, imms) extract(0, 0, 0, 0, rd, rn, rm, imms)
#define extrx(rd, rn, rm, imms) extract(1, 0, 1, 0, rd, rn, rm, imms)

#define shift(sf, type, rd, rn, op2)                                           \
    _Generic(op2,                                                              \
        rasReg: dataproc2source(sf, 0, 8 + type, rd, rn,                       \
                                __FORCE(rasReg, op2)),                         \
        default: __EMIT(PseudoShiftImm, sf, type, rd, rn, __FORCE_INT(op2)))

#define lslw(rd, rn, op2) shift(0, 0, rd, rn, op2)
#define lsrw(rd, rn, op2) shift(0, 1, rd, rn, op2)
#define asrw(rd, rn, op2) shift(0, 2, rd, rn, op2)
#define rorw(rd, rn, op2) shift(0, 3, rd, rn, op2)
#define lslx(rd, rn, op2) shift(1, 0, rd, rn, op2)
#define lsrx(rd, rn, op2) shift(1, 1, rd, rn, op2)
#define asrx(rd, rn, op2) shift(1, 2, rd, rn, op2)
#define rorx(rd, rn, op2) shift(1, 3, rd, rn, op2)

#define _shift(t, name, op, ...)                                               \
    __VA_IF(__shift(name, op, __VA_DFL(0, __VA_ARGS__)), ((rasShift) {op, t}), \
            __VA_ARGS__)
#define __shift(name, op, op1) ___shift(name, op, op1)
#define ___shift(name, op, op1, ...) name(op, op1, __VA_ARGS__)

#define lsl(op, ...) _shift(0, lsl_, op __VA_OPT__(, ) __VA_ARGS__)
#define lsr(op, ...) _shift(1, lsr_, op __VA_OPT__(, ) __VA_ARGS__)
#define asr(op, ...) _shift(2, asr_, op __VA_OPT__(, ) __VA_ARGS__)

#define uxtb_(rd, rn) ubfxw(rd, rn, 0, 8)
#define uxth_(rd, rn) ubfxw(rd, rn, 0, 16)
#define uxtw_(rd, rn) ubfxx(rd, rn, 0, 32)
#define sxtbw(rd, rn) sbfxw(rd, rn, 0, 8)
#define sxtbx(rd, rn) sbfxx(rd, rn, 0, 8)
#define sxthw(rd, rn) sbfxw(rd, rn, 0, 16)
#define sxthx(rd, rn) sbfxx(rd, rn, 0, 16)
#define sxtw_(rd, rn) sbfxx(rd, rn, 0, 32)

#define _extend(t, name, ...) __extend(t, name, __VA_DFL(0, __VA_ARGS__))
#define __extend(t, name, op) ___extend(t, name, op)
#define ___extend(t, name, op, ...)                                            \
    __VA_IF(name(op, __VA_DFL(0, __VA_ARGS__)), ((rasExtend) {op, t}),         \
            __VA_ARGS__)

#define uxtb(...) _extend(0, uxtb_, __VA_ARGS__)
#define uxth(...) _extend(1, uxth_, __VA_ARGS__)
#define uxtw(...) _extend(2, uxtw_, __VA_ARGS__)
#define uxtx(...) _extend(3, movx, __VA_ARGS__)
#define sxtb(...) _extend(4, sxtb_, __VA_ARGS__)
#define sxth(...) _extend(5, sxth_, __VA_ARGS__)
#define sxtw(...) _extend(6, sxtw_, __VA_ARGS__)
#define sxtx(...) _extend(7, movx, __VA_ARGS__)

#define pcreladdr(op, rd, l) __EMIT(PCRelAddr, op, l, rd)

#define adr(rd, l) pcreladdr(0, rd, l)
#define adrp(rd, l) pcreladdr(1, rd, l)
#define adrl(rd, l) __EMIT(PseudoPCRelAddrLong, rd, l)

#define movewide(sf, opc, rd, imm, ...)                                        \
    __EMIT(MoveWide, sf, opc, __VA_DFL(lsl(0), __VA_ARGS__), imm, rd)

#define movnw(rd, imm, ...) movewide(0, 0, rd, imm, __VA_ARGS__)
#define movzw(rd, imm, ...) movewide(0, 2, rd, imm, __VA_ARGS__)
#define movkw(rd, imm, ...) movewide(0, 3, rd, imm, __VA_ARGS__)
#define movnx(rd, imm, ...) movewide(1, 0, rd, imm, __VA_ARGS__)
#define movzx(rd, imm, ...) movewide(1, 2, rd, imm, __VA_ARGS__)
#define movkx(rd, imm, ...) movewide(1, 3, rd, imm, __VA_ARGS__)

#define movw(rd, op2)                                                          \
    _Generic(op2,                                                              \
        rasReg: __EMIT(PseudoMovReg, 0, rd, __FORCE(rasReg, op2)),             \
        default: __EMIT(PseudoMovImm, 0, rd, __FORCE_INT(op2)))
#define movx(rd, op2)                                                          \
    _Generic(op2,                                                              \
        rasReg: __EMIT(PseudoMovReg, 1, rd, __FORCE(rasReg, op2)),             \
        default: __EMIT(PseudoMovImm, 1, rd, __FORCE_INT(op2)))

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

#define __V2R(vn) _Generic(vn, rasVReg: Reg((vn).idx))

#define loadstore(vr, size, opc, rt, amod)                                     \
    _loadstore(vr, size, opc, rt, __EXPAND_AMOD(amod))
#define _loadstore(vr, size, opc, rt, amod) __loadstore(vr, size, opc, rt, amod)
#define __loadstore(vr, size, opc, rt, rn, off, ...)                           \
    _Generic(off,                                                              \
        rasReg: rasEmitLoadStoreRegOff,                                        \
        default: rasEmitLoadStoreImmOff)(                                      \
        RAS_CTX_VAR, size, vr, opc, off,                                       \
        _Generic(off,                                                          \
            rasReg: __MAKE_EXT(__VA_DFL(uxtx(), __VA_ARGS__)),                 \
            default: __VA_DFL(0, __VA_ARGS__)),                                \
        rn, rt)

#define strb(rt, amod) loadstore(0, 0, 0, rt, amod)
#define ldrb(rt, amod) loadstore(0, 0, 1, rt, amod)
#define ldrsbx(rt, amod) loadstore(0, 0, 2, rt, amod)
#define ldrsbw(rt, amod) loadstore(0, 0, 3, rt, amod)
#define strh(rt, amod) loadstore(0, 1, 0, rt, amod)
#define ldrh(rt, amod) loadstore(0, 1, 1, rt, amod)
#define ldrshx(rt, amod) loadstore(0, 1, 2, rt, amod)
#define ldrshw(rt, amod) loadstore(0, 1, 3, rt, amod)
#define strw(rt, amod) loadstore(0, 2, 0, rt, amod)
#define ldrw(rt, amod) loadstore(0, 2, 1, rt, amod)
#define ldrsw(rt, amod) loadstore(0, 2, 2, rt, amod)
#define strx(rt, amod) loadstore(0, 3, 0, rt, amod)
#define ldrx(rt, amod) loadstore(0, 3, 1, rt, amod)

#define strs(vt, amod) loadstore(1, 2, 0, __V2R(vt), amod)
#define ldrs(vt, amod) loadstore(1, 2, 1, __V2R(vt), amod)
#define strd(vt, amod) loadstore(1, 3, 0, __V2R(vt), amod)
#define ldrd(vt, amod) loadstore(1, 3, 1, __V2R(vt), amod)
#define strq(vt, amod) loadstore(1, 0, 2, __V2R(vt), amod)
#define ldrq(vt, amod) loadstore(1, 0, 3, __V2R(vt), amod)
#define pushv(vt) strq(vt, (sp, -0x10, pre))
#define popv(vt) ldrq(vt, (sp, 0x10, post))

#define loadliteral(vr, opc, rt, l) __EMIT(LoadLiteral, opc, vr, l, rt)

#define ldrlw(rt, l) loadliteral(0, 0, rt, l)
#define ldrlx(rt, l) loadliteral(0, 1, rt, l)
#define ldrlsw(rt, l) loadliteral(0, 2, rt, l)

#define ldrls(vt, l) loadliteral(1, 0, __V2R(vt), l)
#define ldrld(vt, l) loadliteral(1, 1, __V2R(vt), l)
#define ldrlq(vt, l) loadliteral(1, 2, __V2R(vt), l)

#define loadstorepair(vr, opc, l, rt, rt2, amod)                               \
    _loadstorepair(vr, opc, l, rt, rt2, __EXPAND_AMOD(amod))
#define _loadstorepair(vr, opc, l, rt, rt2, amod)                              \
    __loadstorepair(vr, opc, l, rt, rt2, amod)
#define __loadstorepair(vr, opc, l, rt, rt2, rn, off, ...)                     \
    __EMIT(LoadStorePair, opc, vr, __VA_DFL(2, __VA_ARGS__), l, off, rt2, rn,  \
           rt)

#define stpw(rt, rt2, amod) loadstorepair(0, 0, 0, rt, rt2, amod)
#define ldpw(rt, rt2, amod) loadstorepair(0, 0, 1, rt, rt2, amod)
#define ldpsw(rt, rt2, amod) loadstorepair(0, 1, 1, rt, rt2, amod)
#define stpx(rt, rt2, amod) loadstorepair(0, 2, 0, rt, rt2, amod)
#define ldpx(rt, rt2, amod) loadstorepair(0, 2, 1, rt, rt2, amod)
#define push(rt, rt2) stpx(rt, rt2, (sp, -0x10, pre))
#define pop(rt, rt2) ldpx(rt, rt2, (sp, 0x10, post))

#define stps(vt, vt2, amod) loadstorepair(1, 0, 0, __V2R(vt), __V2R(vt2), amod)
#define ldps(vt, vt2, amod) loadstorepair(1, 0, 1, __V2R(vt), __V2R(vt2), amod)
#define stpd(vt, vt2, amod) loadstorepair(1, 1, 0, __V2R(vt), __V2R(vt2), amod)
#define ldpd(vt, vt2, amod) loadstorepair(1, 1, 1, __V2R(vt), __V2R(vt2), amod)
#define stpq(vt, vt2, amod) loadstorepair(1, 2, 0, __V2R(vt), __V2R(vt2), amod)
#define ldpq(vt, vt2, amod) loadstorepair(1, 2, 1, __V2R(vt), __V2R(vt2), amod)

#define post 1
#define pre 3

#define branchuncondimm(op, l) __EMIT(BranchUncondImm, op, l)
#define branchcondimm(c, o0, l) __EMIT(BranchCondImm, l, o0, c)

#define b(l, ...)                                                              \
    __VA_IF(branchcondimm(l, 0, __VA_ARGS__), branchuncondimm(0, l),           \
            __VA_ARGS__)
#define bl(l) branchuncondimm(1, l)

#define branchcompimm(sf, op, rt, l) __EMIT(BranchCompImm, sf, op, l, rt)

#define cbzw(rt, l) branchcompimm(0, 0, rt, l)
#define cbnzw(rt, l) branchcompimm(0, 1, rt, l)
#define cbzx(rt, l) branchcompimm(1, 0, rt, l)
#define cbnzx(rt, l) branchcompimm(1, 1, rt, l)

#define branchreg(opc, op2, op3, op4, rn)                                      \
    __EMIT(BranchReg, opc, op2, op3, rn, op4)

#define br(rn) branchreg(0, 31, 0, 0, rn)
#define blr(rn) branchreg(1, 31, 0, 0, rn)
#define ret(...) branchreg(2, 31, 0, 0, __VA_DFL(lr, __VA_ARGS__))

#define eq 0
#define ne 1
#define cs 2
#define cc 3
#define mi 4
#define pl 5
#define vs 6
#define vc 7
#define hi 8
#define ls 9
#define ge 10
#define lt 11
#define gt 12
#define le 13
#define al 14
#define nv 15
#define hs cs
#define lo cc

#define beq(l) b(eq, l)
#define bne(l) b(ne, l)
#define bcs(l) b(cs, l)
#define bcc(l) b(cc, l)
#define bmi(l) b(mi, l)
#define bpl(l) b(pl, l)
#define bvs(l) b(vs, l)
#define bvc(l) b(vc, l)
#define bhi(l) b(hi, l)
#define bls(l) b(ls, l)
#define bge(l) b(ge, l)
#define blt(l) b(lt, l)
#define bgt(l) b(gt, l)
#define ble(l) b(le, l)
#define bal(l) b(al, l)
#define bnv(l) b(nv, l)
#define bhs(l) b(hs, l)
#define blo(l) b(lo, l)

#define hint(opc) __EMIT(Hint, opc)
#define nop() hint(0)

#define systemregmove(l, rt, opc) __EMIT(SystemRegMove, l, opc, rt)

#define msr(opc, rt) systemregmove(0, rt, opc)
#define mrs(rt, opc) systemregmove(1, rt, opc)

#define Label(l, ...) rasLabel l = Lnew(__VA_ARGS__)
#define Lnew(...) __VA_IF(_Lnewext(__VA_ARGS__), _Lnew(), __VA_ARGS__)
#define _Lnew() rasDeclareLabel(RAS_CTX_VAR)
#define _Lnewext(addr) rasDefineLabelExternal(_Lnew(), addr)
#define L(l) rasDefineLabel(RAS_CTX_VAR, l)
#define Lext(l, addr) rasDefineLabelExternal(l, addr)

#define fpmovimm(ftype, m, s, rd, fimm, imm5)                                  \
    __EMIT(FPMovImm, m, s, ftype, fimm, imm5, rd)

#define fpdataproc1source(ftype, m, s, opcode, rd, rn)                         \
    __EMIT(FPDataProc1Source, m, s, ftype, opcode, rn, rd)

#define fmovs(rd, op2)                                                         \
    _Generic(op2,                                                              \
        rasVReg: fpdataproc1source(0, 0, 0, 0, rd, __FORCE(rasVReg, op2)),     \
        default: fpmovimm(0, 0, 0, rd, __FORCE_FLT(op2), 0))
#define fmovd(rd, op2)                                                         \
    _Generic(op2,                                                              \
        rasVReg: fpdataproc1source(1, 0, 0, 0, rd, __FORCE(rasVReg, op2)),     \
        default: fpmovimm(1, 0, 0, rd, __FORCE_FLT(op2), 0))

#define fabss(rd, rn) fpdataproc1source(0, 0, 0, 1, rd, rn)
#define fnegs(rd, rn) fpdataproc1source(0, 0, 0, 2, rd, rn)
#define fsqrts(rd, rn) fpdataproc1source(0, 0, 0, 3, rd, rn)
#define fcvtds(rd, rn) fpdataproc1source(0, 0, 0, 5, rd, rn)
#define fabsd(rd, rn) fpdataproc1source(1, 0, 0, 1, rd, rn)
#define fnegd(rd, rn) fpdataproc1source(1, 0, 0, 2, rd, rn)
#define fsqrtd(rd, rn) fpdataproc1source(1, 0, 0, 3, rd, rn)
#define fcvtsd(rd, rn) fpdataproc1source(1, 0, 0, 4, rd, rn)

#define fpcompare(ftype, m, s, op, opcode2, rn, rm)                            \
    __EMIT(FPCompare, m, s, ftype, rm, op, rn, opcode2)

#define fcmps(rn, rm) fpcompare(0, 0, 0, 0, 0, rn, rm)
#define fcmpzs(rn) fpcompare(0, 0, 0, 0, 8, rn, v0)
#define fcmpd(rn, rm) fpcompare(1, 0, 0, 0, 0, rn, rm)
#define fcmpzd(rn) fpcompare(1, 0, 0, 0, 8, rn, v0)

#define fpdataproc2source(ftype, m, s, opcode, rd, rn, rm)                     \
    __EMIT(FPDataProc2Source, m, s, ftype, rm, opcode, rn, rd)

#define fmuls(rd, rn, rm) fpdataproc2source(0, 0, 0, 0, rd, rn, rm)
#define fdivs(rd, rn, rm) fpdataproc2source(0, 0, 0, 1, rd, rn, rm)
#define fadds(rd, rn, rm) fpdataproc2source(0, 0, 0, 2, rd, rn, rm)
#define fsubs(rd, rn, rm) fpdataproc2source(0, 0, 0, 3, rd, rn, rm)
#define fmaxs(rd, rn, rm) fpdataproc2source(0, 0, 0, 4, rd, rn, rm)
#define fmins(rd, rn, rm) fpdataproc2source(0, 0, 0, 5, rd, rn, rm)
#define fmaxnms(rd, rn, rm) fpdataproc2source(0, 0, 0, 6, rd, rn, rm)
#define fminnms(rd, rn, rm) fpdataproc2source(0, 0, 0, 7, rd, rn, rm)
#define fnmuls(rd, rn, rm) fpdataproc2source(0, 0, 0, 8, rd, rn, rm)
#define fmuld(rd, rn, rm) fpdataproc2source(1, 0, 0, 0, rd, rn, rm)
#define fdivd(rd, rn, rm) fpdataproc2source(1, 0, 0, 1, rd, rn, rm)
#define faddd(rd, rn, rm) fpdataproc2source(1, 0, 0, 2, rd, rn, rm)
#define fsubd(rd, rn, rm) fpdataproc2source(1, 0, 0, 3, rd, rn, rm)
#define fmaxd(rd, rn, rm) fpdataproc2source(1, 0, 0, 4, rd, rn, rm)
#define fmind(rd, rn, rm) fpdataproc2source(1, 0, 0, 5, rd, rn, rm)
#define fmaxnmd(rd, rn, rm) fpdataproc2source(1, 0, 0, 6, rd, rn, rm)
#define fminnmd(rd, rn, rm) fpdataproc2source(1, 0, 0, 7, rd, rn, rm)
#define fnmuld(rd, rn, rm) fpdataproc2source(1, 0, 0, 8, rd, rn, rm)

#define fpdataproc3source(ftype, m, s, o1, o0, rd, rn, rm, ra)                 \
    __EMIT(FPDataProc3Source, m, s, ftype, o1, rm, o0, ra, rn, rd)

#define fmadds(rd, rn, rm, ra) fpdataproc3source(0, 0, 0, 0, 0, rd, rn, rm, ra)
#define fmsubs(rd, rn, rm, ra) fpdataproc3source(0, 0, 0, 0, 1, rd, rn, rm, ra)
#define fnmadds(rd, rn, rm, ra) fpdataproc3source(0, 0, 0, 1, 0, rd, rn, rm, ra)
#define fnmsubs(rd, rn, rm, ra) fpdataproc3source(0, 0, 0, 1, 1, rd, rn, rm, ra)
#define fmaddd(rd, rn, rm, ra) fpdataproc3source(1, 0, 0, 0, 0, rd, rn, rm, ra)
#define fmsubd(rd, rn, rm, ra) fpdataproc3source(1, 0, 0, 0, 1, rd, rn, rm, ra)
#define fnmaddd(rd, rn, rm, ra) fpdataproc3source(1, 0, 0, 1, 0, rd, rn, rm, ra)
#define fnmsubd(rd, rn, rm, ra) fpdataproc3source(1, 0, 0, 1, 1, rd, rn, rm, ra)

#define __R2V(vn)                                                              \
    _Generic(vn,                                                               \
        rasReg: VReg((vn).idx),                                                \
        default: *(rasVReg*) _ras_invalid_argument_type)

#define fpconvertintrv(sf, ftype, s, rmode, opcode, rd, rn)                    \
    __EMIT(FPConvertInt, sf, s, ftype, rmode, opcode, rn, __R2V(rd))
#define fpconvertintvr(sf, ftype, s, rmode, opcode, rd, rn)                    \
    __EMIT(FPConvertInt, sf, s, ftype, rmode, opcode, __R2V(rn), rd)

#define fpmovegpr(sf, rd, rn)                                                  \
    _Generic(rd,                                                               \
        rasReg: fpconvertintrv(sf, sf, 0, 0, 6, rd, __FORCE(rasVReg, rn)),     \
        rasVReg: fpconvertintvr(sf, sf, 0, 0, 7, __FORCE(rasVReg, rd), rn))
#define fmovw(rd, rn) fpmovegpr(0, rd, rn)
#define fmovx(rd, rn) fpmovegpr(1, rd, rn)

#define scvtfsw(rd, rn) fpconvertintvr(0, 0, 0, 0, 2, rd, rn)
#define ucvtfsw(rd, rn) fpconvertintvr(0, 0, 0, 0, 3, rd, rn)
#define scvtfdw(rd, rn) fpconvertintvr(0, 1, 0, 0, 2, rd, rn)
#define ucvtfdw(rd, rn) fpconvertintvr(0, 1, 0, 0, 3, rd, rn)
#define scvtfsx(rd, rn) fpconvertintvr(1, 0, 0, 0, 2, rd, rn)
#define ucvtfsx(rd, rn) fpconvertintvr(1, 0, 0, 0, 3, rd, rn)
#define scvtfdx(rd, rn) fpconvertintvr(1, 1, 0, 0, 2, rd, rn)
#define ucvtfdx(rd, rn) fpconvertintvr(1, 1, 0, 0, 3, rd, rn)

#define fcvtzsws(rd, rn) fpconvertintrv(0, 0, 0, 3, 0, rd, rn)
#define fcvtzuws(rd, rn) fpconvertintrv(0, 0, 0, 3, 1, rd, rn)
#define fcvtzswd(rd, rn) fpconvertintrv(0, 1, 0, 3, 0, rd, rn)
#define fcvtzuwd(rd, rn) fpconvertintrv(0, 1, 0, 3, 1, rd, rn)
#define fcvtzsxs(rd, rn) fpconvertintrv(1, 0, 0, 3, 0, rd, rn)
#define fcvtzuxs(rd, rn) fpconvertintrv(1, 0, 0, 3, 1, rd, rn)
#define fcvtzsxd(rd, rn) fpconvertintrv(1, 1, 0, 3, 0, rd, rn)
#define fcvtzuxd(rd, rn) fpconvertintrv(1, 1, 0, 3, 1, rd, rn)

#define Reg(n) ((rasReg) {n})

#define r0 Reg(0)
#define r1 Reg(1)
#define r2 Reg(2)
#define r3 Reg(3)
#define r4 Reg(4)
#define r5 Reg(5)
#define r6 Reg(6)
#define r7 Reg(7)
#define r8 Reg(8)
#define r9 Reg(9)
#define r10 Reg(10)
#define r11 Reg(11)
#define r12 Reg(12)
#define r13 Reg(13)
#define r14 Reg(14)
#define r15 Reg(15)
#define r16 Reg(16)
#define r17 Reg(17)
#define r18 Reg(18)
#define r19 Reg(19)
#define r20 Reg(20)
#define r21 Reg(21)
#define r22 Reg(22)
#define r23 Reg(23)
#define r24 Reg(24)
#define r25 Reg(25)
#define r26 Reg(26)
#define r27 Reg(27)
#define r28 Reg(28)
#define r29 Reg(29)
#define r30 Reg(30)
#define ip0 r16
#define ip1 r17
#define fp r29
#define lr r30
#define zr ((rasReg) {31, 0})
#define sp ((rasReg) {31, 1})

#define VReg(n) ((rasVReg) {n})

#define v0 VReg(0)
#define v1 VReg(1)
#define v2 VReg(2)
#define v3 VReg(3)
#define v4 VReg(4)
#define v5 VReg(5)
#define v6 VReg(6)
#define v7 VReg(7)
#define v8 VReg(8)
#define v9 VReg(9)
#define v10 VReg(10)
#define v11 VReg(11)
#define v12 VReg(12)
#define v13 VReg(13)
#define v14 VReg(14)
#define v15 VReg(15)
#define v16 VReg(16)
#define v17 VReg(17)
#define v18 VReg(18)
#define v19 VReg(19)
#define v20 VReg(20)
#define v21 VReg(21)
#define v22 VReg(22)
#define v23 VReg(23)
#define v24 VReg(24)
#define v25 VReg(25)
#define v26 VReg(26)
#define v27 VReg(27)
#define v28 VReg(28)
#define v29 VReg(29)
#define v30 VReg(30)
#define v31 VReg(31)

#define nzcv 0xda10

#ifdef RAS_DEFAULT_SUFFIX
#define __CAT(x, y) ___CAT(x, y)
#define ___CAT(x, y) x##y
#define _(x) __CAT(x, RAS_DEFAULT_SUFFIX)

#define add _(add)
#define sub _(sub)
#define adds _(adds)
#define subs _(subs)
#define cmp _(cmp)
#define cmn _(cmn)
#define adc _(adc)
#define sbc _(sbc)
#define adcs _(adcs)
#define sbcs _(sbcs)
#define and _(and)
#define bic _(bic)
#define orr _(orr)
#define orn _(orn)
#define eor _(eor)
#define eon _(eon)
#define ands _(ands)
#define bics _(bics)
#define mvn _(mvn)
#define tst _(tst)
#define rbit _(rbit)
#define rev _(rev)
#define rev16 rev16w
#define clz _(clz)
#define cls _(cls)
#define udiv _(udiv)
#define sdiv _(sdiv)
#define lslv _(lslv)
#define lsrv _(lsrv)
#define asrv _(asrv)
#define rorv _(rorv)
#define madd _(madd)
#define msub _(msub)
#define mul _(mul)
#define mneg _(mneg)
#define csel _(csel)
#define csinc _(csinc)
#define csinv _(csinv)
#define csneg _(csneg)
#define cmov _(cmov)
#define cset _(cset)
#define csetm _(csetm)
#define cinc _(cinc)
#define cinv _(cinv)
#define cneg _(cneg)
#define sbfm _(sbfm)
#define bfm _(bfm)
#define ubfm _(ubfm)
#define sbfiz _(sbfiz)
#define sbfx _(sbfx)
#define bfi _(bfi)
#define bfc _(bfc)
#define bfxil _(bfxil)
#define ubfiz _(ubfiz)
#define ubfx _(ubfx)
#define extr _(extr)
#define lsl_ _(lsl)
#define lsr_ _(lsr)
#define asr_ _(asr)
#define ror _(ror)
#define sxtb_ _(sxtb)
#define sxth_ _(sxth)
#define movn _(movn)
#define movz _(movz)
#define movk _(movk)
#define mov _(mov)
#define ldrsb _(ldrsb)
#define ldrsh _(ldrsh)
#define str _(str)
#define ldr _(ldr)
#define ldrl _(ldrl)
#define stp _(stp)
#define ldp _(ldp)
#define cbz _(cbz)
#define cbnz _(cbnz)

#endif

#endif