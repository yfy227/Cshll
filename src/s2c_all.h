/*
 * s2c_all.h — internal master header for the shell2c transpiler.
 * Single include point for every compiler translation unit.
 *
 * Module map (each .c includes this header):
 *   src/vm_compiler.c   — VM bytecode compiler + arithmetic compiler (L1)
 *   src/symtab.c        — symbol tables: vars, funcs, heredocs (L0/L2)
 *   src/ast.c           — AST constructors + free_node (L3)
 *   src/tokenizer.c     — lexer (L4)
 *   src/translate.c     — expression translation (L5)
 *   src/expand.c        — string expansion (L6)
 *   src/cond.c          — condition translation (L7)
 *   src/emit.c          — C code emitter (L8)
 *   src/parse.c         — parser (L9)
 *   src/runtime_data.c  — RT_HEADER string (emitted into output) (L10)
 *   src/s2c_main.c      — prescan, CLI main, threading (L11)
 *
 * NOTE: s2c_vm_compiler.h is NOT included here — it declares the
 * standalone VmCompiler interface used only by s2c_vm_bridge.c.
 * vm_compiler.c uses its own VmCompilerState (legacy in-tree VM path).
 */
#ifndef S2C_ALL_H
#define S2C_ALL_H

#include "s2c_common.h"
#include "s2c_symtab.h"
#include "s2c_ast.h"
#include "s2c_emit.h"
#include "s2c_parse.h"
#include "s2c_obfuscate.h"
#include "s2c_mangle.h"
#include "s2c_vm_isa.h"
#include "s2c_vm_runtime.h"
#include <pthread.h>

#endif /* S2C_ALL_H */
