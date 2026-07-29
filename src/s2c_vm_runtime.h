/*
 * s2c_vm_runtime.h — VM runtime declarations
 * The VM interpreter is emitted into the generated C file as a string.
 * This header defines the interface for the transpiler side.
 *
 * Author: 爱摸鱼的狐狸 🦊 (VM extension)
 */
#ifndef S2C_VM_RUNTIME_H
#define S2C_VM_RUNTIME_H

#include <stdio.h>
#include "s2c_vm_isa.h"

/* The runtime C code string, emitted into generated output */
extern const char *VM_RUNTIME;

/* Emit the VM runtime (interpreter) into the output file */
void emit_vm_runtime(FILE *out);

/* Emit the bytecode constant pool */
void emit_vm_const_pool(FILE *out, const char **consts, int nconsts);

/* Emit the bytecode as a hex-encoded byte array */
void emit_vm_code(FILE *out, const uint8_t *code, int len);

/* Emit the function table */
void emit_vm_func_table(FILE *out, const VmFuncEntry *funcs, int nfuncs,
                        const char **const_pool);

/* Emit the VM entry point (main function that starts the VM) */
void emit_vm_main(FILE *out);

#endif /* S2C_VM_RUNTIME_H */
