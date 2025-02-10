#include "register_allocator.h"

#include <stdio.h>
#include <stdlib.h>

void find_uses(IRBlock* block, u32* vuses) {
    for (int i = 0; i < block->code.size; i++) {
        vuses[i] = 0;
        IRInstr inst = block->code.d[i];
        if (!inst.imm1) vuses[inst.op1]++;
        if (!inst.imm2) vuses[inst.op2]++;
    }
}

RegAllocation allocate_registers(IRBlock* block) {
    u32 vuses[block->code.size];
    find_uses(block, vuses);

    RegAllocation ret;
    Vec_init(ret.reg_info);
    ret.reg_assn = malloc(block->code.size * sizeof(u32));
    ret.nassns = block->code.size;
    Vector(bool) reg_active = {};

    for (int i = 0; i < block->code.size; i++) {
        IRInstr inst = block->code.d[i];

        if (!inst.imm1) {
            if (!--vuses[inst.op1]) {
                reg_active.d[ret.reg_assn[inst.op1]] = false;
            }
        }
        if (!inst.imm2) {
            if (!--vuses[inst.op2]) {
                reg_active.d[ret.reg_assn[inst.op2]] = false;
            }
        }

        if (iropc_iscallback(inst.opcode)) {
            for (int r = 0; r < ret.reg_info.size; r++) {
                if (reg_active.d[r]) ret.reg_info.d[r].type = REG_SAVED;
            }
        }

        if (iropc_hasresult(inst.opcode)) {
            u32 assignment = -1;
            for (int r = 0; r < reg_active.size; r++) {
                if (!reg_active.d[r]) {
                    assignment = r;
                    break;
                }
            }
            if (assignment == -1) {
                assignment = reg_active.size;
                Vec_push(ret.reg_info, ((RegInfo) {0, REG_TEMP}));
                Vec_push(reg_active, true);
            }
            ret.reg_info.d[assignment].uses += 1 + vuses[i];
            reg_active.d[assignment] = true;
            ret.reg_assn[i] = assignment;
            if (!vuses[i]) reg_active.d[assignment] = false;
        } else {
            ret.reg_assn[i] = -1;
        }
    }

    Vec_free(reg_active);

    return ret;
}

void regalloc_free(RegAllocation* regalloc) {
    Vec_free(regalloc->reg_info);
    free(regalloc->reg_assn);
}

RegAllocation* regalloc_cmp;
int compare(const void* i, const void* j) {
    return regalloc_cmp->reg_info.d[*(int*) j].uses -
           regalloc_cmp->reg_info.d[*(int*) i].uses;
}

HostRegAllocation allocate_host_registers(RegAllocation* regalloc, u32 ntemp,
                                          u32 nsaved) {
    int nregs = regalloc->reg_info.size;

    HostRegAllocation ret = {};
    ret.nregs = nregs;
    if (!nregs) return ret;

    ret.hostreg_info = calloc(nregs, sizeof(HostRegInfo));
    int sorted[nregs];
    for (int i = 0; i < nregs; i++) sorted[i] = i;
    regalloc_cmp = regalloc;
    qsort(sorted, nregs, sizeof(int), compare);

    for (int _i = 0; _i < nregs; _i++) {
        int i = sorted[_i];
        if (ret.hostreg_info[i].type == REG_NONE &&
            regalloc->reg_info.d[i].type == REG_TEMP) {
            ret.hostreg_info[i].type = REG_TEMP;
            ret.hostreg_info[i].index = ret.count[REG_TEMP]++;
            if (ret.count[REG_TEMP] == ntemp) break;
        }
    }
    for (int _i = 0; _i < nregs; _i++) {
        int i = sorted[_i];
        if (ret.hostreg_info[i].type == REG_NONE) {
            ret.hostreg_info[i].type = REG_SAVED;
            ret.hostreg_info[i].index = ret.count[REG_SAVED]++;
            if (ret.count[REG_SAVED] == nsaved) break;
        }
    }
    for (int i = 0; i < nregs; i++) {
        if (ret.hostreg_info[i].type == REG_NONE) {
            ret.hostreg_info[i].type = REG_STACK;
            ret.hostreg_info[i].index = ret.count[REG_STACK]++;
        }
    }

    return ret;
}

void hostregalloc_free(HostRegAllocation* hostregs) {
    free(hostregs->hostreg_info);
}

void regalloc_print(RegAllocation* regalloc) {
    static const char* typenames[] = {"", "tmp", "sav", "stk"};

    printf("Registers:");
    for (int i = 0; i < regalloc->reg_info.size; i++) {
        printf(" $%d(%s,%d)", i, typenames[regalloc->reg_info.d[i].type],
               regalloc->reg_info.d[i].uses);
    }
    printf("\nAssignments:");
    for (int i = 0; i < regalloc->nassns; i++) {
        u32 assn = regalloc->reg_assn[i];
        if (assn == -1) continue;
        printf(" v%d:$%d", i, assn);
    }
    printf("\n");
}