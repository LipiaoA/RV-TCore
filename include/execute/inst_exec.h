// include/execute/inst_exec.h
#ifndef RV_TCORE_INST_EXEC_H
#define RV_TCORE_INST_EXEC_H

#include "decode/inst.h"

namespace InstExec {
bool exec(MInstPtr inst);
bool exec_alu(MInstPtr inst);
bool exec_bru(MInstPtr inst);
bool exec_agu(MInstPtr inst);
bool exec_fpu(MInstPtr inst);

}

#endif