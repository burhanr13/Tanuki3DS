#include "vfp.h"

#include <math.h>

void exec_vfp_data_proc(ArmCore* cpu, ArmInstr instr) {
    bool dp = instr.cp_data_proc.cpnum & 1;
    u32 vd = instr.cp_data_proc.crd;
    u32 vn = instr.cp_data_proc.crn;
    u32 vm = instr.cp_data_proc.crm;
    if (!dp) {
        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
        vn = vn << 1 | (instr.cp_data_proc.cp >> 2);
        vm = vm << 1 | (instr.cp_data_proc.cp & 1);
    }
    bool op = instr.cp_data_proc.cp & 2;

    switch (instr.cp_data_proc.cpopc & 0b1011) {
        case 0:
            if (op) {
                if (dp) {
                    cpu->d[vd] -= cpu->d[vn] * cpu->d[vm];
                } else {
                    cpu->s[vd] -= cpu->s[vn] * cpu->s[vm];
                }
            } else {
                if (dp) {
                    cpu->d[vd] += cpu->d[vn] * cpu->d[vm];
                } else {
                    cpu->s[vd] += cpu->s[vn] * cpu->s[vm];
                }
            }
            break;
        case 1:
            if (op) {
                if (dp) {
                    cpu->d[vd] += cpu->d[vn] * cpu->d[vm];
                    cpu->d[vd] = -cpu->d[vd];
                } else {
                    cpu->s[vd] += cpu->s[vn] * cpu->s[vm];
                    cpu->s[vd] = -cpu->s[vd];
                }
            } else {
                if (dp) {
                    cpu->d[vd] -= cpu->d[vn] * cpu->d[vm];
                    cpu->d[vd] = -cpu->d[vd];
                } else {
                    cpu->s[vd] -= cpu->s[vn] * cpu->s[vm];
                    cpu->s[vd] = -cpu->s[vd];
                }
            }
            break;
        case 2:
            if (dp) {
                cpu->d[vd] = cpu->d[vn] * cpu->d[vm];
                if (op) cpu->d[vd] = -cpu->d[vd];
            } else {
                cpu->s[vd] = cpu->s[vn] * cpu->s[vm];
                if (op) cpu->s[vd] = -cpu->s[vd];
            }
            break;
        case 3:
            if (op) {
                if (dp) {
                    cpu->d[vd] = cpu->d[vn] - cpu->d[vm];
                } else {
                    cpu->s[vd] = cpu->s[vn] - cpu->s[vm];
                }
            } else {
                if (dp) {
                    cpu->d[vd] = cpu->d[vn] + cpu->d[vm];
                } else {
                    cpu->s[vd] = cpu->s[vn] + cpu->s[vm];
                }
            }
            break;
        case 8:
            if (dp) {
                cpu->d[vd] = cpu->d[vn] / cpu->d[vm];
            } else {
                cpu->s[vd] = cpu->s[vn] / cpu->s[vm];
            }
            break;
        case 11: {
            op = instr.cp_data_proc.cp & 4;
            switch (instr.cp_data_proc.crn) {
                case 0:
                    if (op) {
                        if (dp) {
                            cpu->d[vd] = fabs(cpu->d[vm]);
                        } else {
                            cpu->s[vd] = fabsf(cpu->s[vm]);
                        }
                    } else {
                        if (dp) {
                            cpu->d[vd] = cpu->d[vm];
                        } else {
                            cpu->s[vd] = cpu->s[vm];
                        }
                    }
                    break;
                case 1:
                    if (op) {
                        if (dp) {
                            cpu->d[vd] = sqrt(cpu->d[vm]);
                        } else {
                            cpu->s[vd] = sqrtf(cpu->s[vm]);
                        }
                    } else {
                        if (dp) {
                            cpu->d[vd] = -cpu->d[vm];
                        } else {
                            cpu->s[vd] = -cpu->s[vm];
                        }
                    }
                    break;
                case 4:
                case 5:
                    if (dp) {
                        double a = cpu->d[vd];
                        double b =
                            (instr.cp_data_proc.crn & 1) ? 0 : cpu->d[vm];
                        if (a == b) {
                            cpu->fpscr.nzcv = 0b0110;
                        } else if (a < b) {
                            cpu->fpscr.nzcv = 0b1000;
                        } else if (a > b) {
                            cpu->fpscr.nzcv = 0b0010;
                        } else {
                            cpu->fpscr.nzcv = 0b0011;
                        }
                    } else {
                        float a = cpu->s[vd];
                        float b = (instr.cp_data_proc.crn & 1) ? 0 : cpu->s[vm];
                        if (a == b) {
                            cpu->fpscr.nzcv = 0b0110;
                        } else if (a < b) {
                            cpu->fpscr.nzcv = 0b1000;
                        } else if (a > b) {
                            cpu->fpscr.nzcv = 0b0010;
                        } else {
                            cpu->fpscr.nzcv = 0b0011;
                        }
                    }
                    break;
                case 7:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        cpu->s[vd] = cpu->d[vm];
                    } else {
                        vd = vd >> 1;
                        cpu->d[vd] = cpu->s[vm];
                    }
                    break;
                case 8:
                    if (dp) {
                        vm = vm << 1 | (instr.cp_data_proc.cp & 1);
                        if (op) {
                            cpu->d[vd] = (s32) F2I(cpu->s[vm]);
                        } else {
                            cpu->d[vd] = F2I(cpu->s[vm]);
                        }
                    } else {
                        if (op) {
                            cpu->s[vd] = (s32) F2I(cpu->s[vm]);
                        } else {
                            cpu->s[vd] = F2I(cpu->s[vm]);
                        }
                    }
                    break;
                case 12:
                case 13:
                    if (dp) {
                        vd = vd << 1 | ((instr.cp_data_proc.cpopc >> 2) & 1);
                        if (instr.cp_data_proc.crn & 1) {
                            cpu->s[vd] = I2F((s32) cpu->d[vm]);
                        } else {
                            cpu->s[vd] = I2F(cpu->d[vm]);
                        }
                    } else {
                        if (instr.cp_data_proc.crn & 1) {
                            cpu->s[vd] = I2F((s32) cpu->s[vm]);
                        } else {
                            cpu->s[vd] = I2F(cpu->s[vm]);
                        }
                    }
                    break;
            }
            break;
        }
    }
}

void exec_vfp_load_mem(ArmCore* cpu, ArmInstr instr, u32 addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            cpu->d[(vd + i) & 15] = cpu->readf64(cpu, addr + 8 * i);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            cpu->s[(vd + i) & 31] = cpu->readf32(cpu, addr + 4 * i);
        }
    }
}

void exec_vfp_store_mem(ArmCore* cpu, ArmInstr instr, u32 addr) {
    u32 rcount;
    if (instr.cp_data_trans.p && !instr.cp_data_trans.w) {
        rcount = 1;
    } else {
        rcount = instr.cp_data_trans.offset;
        if (instr.cp_data_trans.cpnum & 1) rcount >>= 1;
    }

    u32 vd = instr.cp_data_trans.crd;

    if (instr.cp_data_trans.cpnum & 1) {
        for (int i = 0; i < rcount; i++) {
            cpu->writef64(cpu, addr + 8 * i, cpu->d[(vd + i) & 15]);
        }
    } else {
        vd = vd << 1 | instr.cp_data_trans.n;

        for (int i = 0; i < rcount; i++) {
            cpu->writef32(cpu, addr + 4 * i, cpu->s[(vd + i) & 31]);
        }
    }
}

u32 exec_vfp_read(ArmCore* cpu, ArmInstr instr) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            return cpu->fpscr.w;
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
            return 0;
        }
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    return F2I(cpu->s[vn]);
}

void exec_vfp_write(ArmCore* cpu, ArmInstr instr, u32 data) {
    if (instr.cp_reg_trans.cpopc == 7) {
        if (instr.cp_reg_trans.crn == 1) {
            cpu->fpscr.w = data;
        } else {
            lwarn("unknown vfp special reg %d", instr.cp_reg_trans.crn);
        }
        return;
    }

    u32 vn = instr.cp_reg_trans.crn << 1;
    if (instr.cp_reg_trans.cpnum & 1) vn |= instr.cp_reg_trans.cpopc & 1;
    else vn |= instr.cp_reg_trans.cp >> 2;

    cpu->s[vn] = I2F(data);
}

u64 exec_vfp_read64(ArmCore* cpu, ArmInstr instr) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        return BITCAST(double, u64, cpu->d[vm]);
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        u64 res = F2I(cpu->s[vm]);
        if (vm < 31) res |= (u64) F2I(cpu->s[vm + 1]) << 32;
        return res;
    }
}

void exec_vfp_write64(ArmCore* cpu, ArmInstr instr, u64 data) {
    if (instr.cp_double_reg_trans.cpnum & 1) {
        u32 vm = instr.cp_double_reg_trans.crm;
        cpu->d[vm] = BITCAST(u64, double, data);
    } else {
        u32 vm = instr.cp_double_reg_trans.crm << 1 |
                 ((instr.cp_double_reg_trans.cp >> 1) & 1);
        cpu->s[vm] = I2F(data);
        if (vm < 31) cpu->s[vm + 1] = I2F(data >> 32);
    }
}