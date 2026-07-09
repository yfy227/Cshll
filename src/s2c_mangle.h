/*
 * s2c_mangle.h — Name mangling for anti-AI-analysis
 * Transforms readable __sh_* names into opaque, non-structural identifiers
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_MANGLE_H
#define S2C_MANGLE_H

/*
 * Name mangling strategy:
 *
 * Instead of __sh_puts → _Z3putsv (C++ style), we use a custom scheme:
 *
 * 1. All __sh_* → _QxNNNN (hex hash of original name)
 * 2. All __b_*  → _QyNNNN
 * 3. All __tw_* → _QzNNNN
 * 4. All __fl*  → _QwNNNN
 * 5. All __fa*  → _QvNNNN
 * 6. All __sfd_* → _QuNNNN
 * 7. All __pfd* → _QtNNNN
 * 8. All temp vars (__rb, __sp, etc) → _QsNNNN
 *
 * The hash is deterministic (same input → same output) so the
 * transpiler and runtime stay consistent within one compilation.
 *
 * When --obfuscate is NOT set, names remain readable (__sh_puts etc).
 * When --obfuscate IS set, all names are mangled.
 */

/* Initialize the mangler with a seed (for deterministic output) */
void mangle_init(int obfuscate_mode);

/* Mangle a name. Returns the mangled name if obfuscation is on,
 * otherwise returns the original name unchanged. */
const char *mangle(const char *name);

/* Specific manglers for different prefixes */
const char *mangle_sh(const char *name);   /* __sh_* → _Qx* */
const char *mangle_b(const char *name);    /* __b_*  → _Qy* */
const char *mangle_tmp(const char *name);  /* __tw_* → _Qz* */

/* Mangle a printf format string: replace __sh_* references in string
 * literals with their mangled equivalents */
const char *mangle_string(const char *s);

/* Global flag */
extern int g_obfuscate;

#endif /* S2C_MANGLE_H */
