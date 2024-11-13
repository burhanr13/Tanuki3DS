#include "arm_core.h"

#include <stdlib.h>

// #include "thumb.h"

// void cpu_fetch_instr(ArmCore* cpu) {
//     cpu->cur_instr = cpu->next_instr;
//     if (cpu->cpsr.t) {
//         cpu->next_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
//         cpu->cur_instr_addr += 2;
//         cpu->next_instr_addr += 2;
//         cpu->pc += 2;
//     } else {
//         cpu->next_instr.w = cpu->fetch32(cpu, cpu->pc);
//         cpu->cur_instr_addr += 4;
//         cpu->next_instr_addr += 4;
//         cpu->pc += 4;
//     }
// }

// void cpu_flush(ArmCore* cpu) {
//     if (cpu->cpsr.t) {
//         cpu->pc &= ~1;
//         cpu->cur_instr_addr = cpu->pc;
//         cpu->cur_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
//         cpu->pc += 2;
//         cpu->next_instr_addr = cpu->pc;
//         cpu->next_instr = thumb_lookup[cpu->fetch16(cpu, cpu->pc)];
//         cpu->pc += 2;
//     } else {
//         cpu->pc &= ~0b11;
//         cpu->cur_instr_addr = cpu->pc;
//         cpu->cur_instr.w = cpu->fetch32(cpu, cpu->pc);
//         cpu->pc += 4;
//         cpu->next_instr_addr = cpu->pc;
//         cpu->next_instr.w = cpu->fetch32(cpu, cpu->pc);
//         cpu->pc += 4;
//     }
// }

RegBank get_bank(CpuMode mode) {
    switch (mode) {
        case M_USER:
            return B_USER;
        case M_FIQ:
            return B_FIQ;
        case M_IRQ:
            return B_IRQ;
        case M_SVC:
            return B_SVC;
        case M_ABT:
            return B_ABT;
        case M_UND:
            return B_UND;
        case M_SYSTEM:
            return B_USER;
    }
    return B_USER;
}

void cpu_update_mode(ArmCore* cpu, CpuMode old) {
    if (cpu->cpsr.m == old) return;
    RegBank old_bank = get_bank(old);
    cpu->banked_sp[old_bank] = cpu->sp;
    cpu->banked_lr[old_bank] = cpu->lr;
    cpu->banked_spsr[old_bank] = cpu->spsr;
    RegBank new_bank = get_bank(cpu->cpsr.m);
    cpu->sp = cpu->banked_sp[new_bank];
    cpu->lr = cpu->banked_lr[new_bank];
    cpu->spsr = cpu->banked_spsr[new_bank];
    if (old == M_FIQ && cpu->cpsr.m != M_FIQ) {
        for (int i = 0; i < 5; i++) {
            cpu->banked_r8_12[1][i] = cpu->r[8 + i];
            cpu->r[8 + i] = cpu->banked_r8_12[0][i];
        }
    }
    if (old != M_FIQ && cpu->cpsr.m == M_FIQ) {
        for (int i = 0; i < 5; i++) {
            cpu->banked_r8_12[0][i] = cpu->r[8 + i];
            cpu->r[8 + i] = cpu->banked_r8_12[1][i];
        }
    }
    switch (cpu->cpsr.m) {
        case M_USER:
        case M_FIQ:
        case M_IRQ:
        case M_SVC:
        case M_ABT:
        case M_UND:
        case M_SYSTEM:
            return;
        default:
            lerror("illegal cpu mode %x (pc = %08x)", cpu->cpsr.m, cpu->pc);
    }
}

// void cpu_handle_exception(ArmCore* cpu, CpuException intr) {
//     if (cpu->pending_flush) {
//         cpu_flush(cpu);
//         cpu->pending_flush = false;
//     }

//     CpuMode old = cpu->cpsr.m;
//     u32 spsr = cpu->cpsr.w;
//     switch (intr) {
//         case E_RESET:
//         case E_SWI:
//         case E_ADDR:
//             cpu->cpsr.m = M_SVC;
//             break;
//         case E_PABT:
//         case E_DABT:
//             cpu->cpsr.m = M_ABT;
//             break;
//         case E_UND:
//             cpu->cpsr.m = M_UND;
//             break;
//         case E_IRQ:
//             cpu->cpsr.m = M_IRQ;
//             break;
//         case E_FIQ:
//             cpu->cpsr.m = M_FIQ;
//             break;
//     }
//     cpu_update_mode(cpu, old);
//     cpu->spsr = spsr;
//     cpu->lr = cpu->pc;
//     if (cpu->cpsr.t) {
//         if (intr == E_SWI || intr == E_UND) cpu->lr -= 2;
//     } else cpu->lr -= 4;
//     cpu->cpsr.t = 0;
//     cpu->cpsr.i = 1;
//     cpu->pc = cpu->vector_base + 4 * intr;
//     cpu_flush(cpu);
// }

void cpu_undefined_fail(ArmCore* cpu, u32 instr) {
    lerror("executing undefined instruction %08x near %08x", instr, cpu->pc);
}

char* mode_name(CpuMode m) {
    switch (m) {
        case M_USER:
            return "USER";
        case M_FIQ:
            return "FIQ";
        case M_IRQ:
            return "IRQ";
        case M_SVC:
            return "SVC";
        case M_ABT:
            return "ABT";
        case M_UND:
            return "UND";
        case M_SYSTEM:
            return "SYSTEM";
        default:
            return "ILLEGAL";
    }
}

void cpu_print_state(ArmCore* cpu) {
    static char* reg_names[16] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                  "r6", "r7", "r8", "r9", "r10", "r11",
                                  "ip", "sp", "lr", "pc"};
    for (int i = 0; i < 4; i++) {
        if (i == 0) printf("CPU\t");
        else printf("\t");
        for (int j = 0; j < 4; j++) {
            printf("%3s=0x%08x\t", reg_names[4 * i + j], cpu->r[4 * i + j]);
        }
        printf("\n");
    }
    printf("\tcpsr=%08x (n=%d,z=%d,c=%d,v=%d,q=%d,i=%d,f=%d,t=%d,m=%s)\n",
           cpu->cpsr.w, cpu->cpsr.n, cpu->cpsr.z, cpu->cpsr.c, cpu->cpsr.v,
           cpu->cpsr.q, cpu->cpsr.i, cpu->cpsr.v, cpu->cpsr.t,
           mode_name(cpu->cpsr.m));
}

void cpu_print_vfp_state(ArmCore* cpu) {
    for (int i = 0; i < 8; i++) {
        printf("\t");
        for (int j = 0; j < 4; j++) {
            printf("s%d=%f\t", 4 * i + j, cpu->s[4 * i + j]);
        }
        printf("\n\t");
        for (int j = 0; j < 4; j++) {
            printf("(0x%08x)\t", cpu->is[4 * i + j]);
        }
        printf("\n");
    }
    for (int i = 0; i < 4; i++) {
        printf("\t");
        for (int j = 0; j < 4; j++) {
            printf("d%d=%lf\t", 4 * i + j, cpu->d[4 * i + j]);
        }
        printf("\n");
    }
}

// void cpu_print_cur_instr(ArmCore* cpu) {
//     if (cpu->cpsr.t) {
//         ThumbInstr instr = {cpu->fetch16(cpu, cpu->cur_instr_addr)};
//         printf("%08x: %04x ", cpu->cur_instr_addr, instr.h);
//         thumb_disassemble(instr, cpu->cur_instr_addr, stdout);
//         printf("\n");
//     } else {
//         printf("%08x: %08x ", cpu->cur_instr_addr, cpu->cur_instr.w);
//         arm_disassemble(cpu->cur_instr, cpu->cur_instr_addr, stdout);
//         printf("\n");
//     }
// }