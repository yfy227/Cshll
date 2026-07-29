/*
 * s2c_vm_compiler.h — Bytecode compiler: Shell AST → VM bytecode
 *
 * Walks the shell AST and emits VM bytecode.
 * Manages:
 *   - Constant pool (string interning)
 *   - Jump target resolution (forward references)
 *   - Function table
 *   - Basic block profiling markers
 *
 * Author: 爱摸鱼的狐狸 🦊 (VM extension)
 */
#ifndef S2C_VM_COMPILER_H
#define S2C_VM_COMPILER_H

#include <stdint.h>
#include <stdio.h>
#include "s2c_vm_isa.h"

/* Bytecode buffer */
typedef struct {
    uint8_t *code;
    int len;
    int cap;
} ByteBuf;

/* Constant pool */
typedef struct {
    char **consts;
    int count;
    int cap;
} ConstPool;

/* Jump patch list (forward references) */
typedef struct {
    int *patches;  /* code offsets that need patching */
    int *targets;   /* target labels */
    int count;
    int cap;
} PatchList;

/* Function table */
typedef struct {
    VmFuncEntry *funcs;
    int count;
    int cap;
} FuncTable;

/* Compiler state */
typedef struct {
    ByteBuf code;
    ConstPool consts;
    PatchList patches;
    FuncTable funcs;
    int next_block_id;  /* for profiling */
    int loop_depth;     /* for break/continue */
    int loop_start[64]; /* stack of loop start PCs */
    int loop_break[64]; /* stack of break patch lists */
} VmCompiler;

/* Initialize/destroy compiler */
void vmc_init(VmCompiler *c);
void vmc_free(VmCompiler *c);

/* Constant pool: intern a string, return its index */
int vmc_intern(VmCompiler *c, const char *s);

/* Bytecode emission */
void vmc_emit(VmCompiler *c, uint8_t op);
void vmc_emit_u16(VmCompiler *c, uint8_t op, uint16_t val);
void vmc_emit_i32(VmCompiler *c, uint8_t op, int32_t val);
int  vmc_emit_jump(VmCompiler *c, uint8_t op);  /* returns patch location */
void vmc_patch_jump(VmCompiler *c, int patch_loc, uint16_t target);

/* Current code position */
int vmc_pc(VmCompiler *c);

/* Compile AST node to bytecode */
void vmc_compile_node(VmCompiler *c, void *node_ptr);
void vmc_compile_block(VmCompiler *c, void *node_ptr);

/* Finalize: resolve all patches, produce final bytecode */
void vmc_finalize(VmCompiler *c);

#endif /* S2C_VM_COMPILER_H */
