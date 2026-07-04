/*
 * s2c_obfuscate.h — Runtime code obfuscation for generated C
 * Makes generated code harder to analyze by AI or reverse engineers
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_OBFUSCATE_H
#define S2C_OBFUSCATE_H

#include <stdio.h>

/*
 * Obfuscation strategies for generated C code:
 *
 * 1. Opaque function names: __sh_* → random-looking _Z* names
 * 2. String obfuscation: XOR-encoded string literals decoded at runtime
 * 3. Control flow flattening: wrap sequential code in switch-dispatch
 * 4. Indirect calls: function pointers instead of direct calls
 * 5. Dead code injection: meaningless statements that look real
 * 6. Variable name mangling: predictable names → non-obvious names
 */

/* XOR-based string decoder — emitted into generated C */
#define S2C_XOR_KEY 0x5A

/* Emit the obfuscation runtime helpers into the output file */
void emit_obfuscation_runtime(FILE *out);

/* Obfuscate a string literal: emit encoded bytes + decode call */
void emit_obfuscated_string(FILE *out, const char *str, const char *var_name);

/* Generate a mangled identifier from a seed */
const char *mangle_name(int seed);

/* Emit a dead-code decoy block */
void emit_decoy_block(FILE *out, int seed);

/* Emit control-flow-flattened wrapper for a code block */
void emit_flattened_block(FILE *out, const char *code, int id);

#endif /* S2C_OBFUSCATE_H */
