// include/execute/inst_exec.h
#ifndef RV_TCORE_INST_EXEC_H
#define RV_TCORE_INST_EXEC_H

#include "decode/inst.h"

namespace InstExec {

typedef struct {
    bool valid { false };
    bool write_rd { false };
    u64 value { 0 };
} AluExecResult;

typedef struct {
    bool valid { false };
    bool taken { false };
    pc_t target { 0 };
    bool write_rd { false };
    u64 link { 0 };
} BruExecResult;

typedef struct {
    bool valid { false };
    u64 addr { 0 };
} AguExecResult;

typedef struct {
    bool valid { false };
    bool unsupported { false };
    u64 value { 0 };
} FpuExecResult;

bool exec(MInstPtr inst);
AluExecResult exec_alu(MInstPtr inst);
BruExecResult exec_bru(MInstPtr inst);
AguExecResult exec_agu(MInstPtr inst);
FpuExecResult exec_fpu(MInstPtr inst);

}

#endif