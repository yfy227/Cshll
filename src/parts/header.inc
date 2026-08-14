/*
 * shell2c.c — Deep Shell-to-C Transpiler (Modular Entry Point)
 *
 * Author: 爱摸鱼的狐狸 🦊
 *
 * Modular architecture with code obfuscation for generated C output.
 * Headers in src/ define the module interfaces; this file contains
 * the full implementation compiled as a single translation unit for
 * simplicity (amalgamation build).
 *
 * Build:  gcc -O2 -Wall -o shell2c shell2c.c src/s2c_obfuscate.c
 * Use:    ./shell2c input.sh output.c [--makefile] [--run] [--obfuscate]
 *
 * Modules:
 *   s2c_common.h    — shared types, utilities, safe-naming
 *   s2c_symtab.h    — variable/function/heredoc tables
 *   s2c_ast.h       — AST node types and Redir struct
 *   s2c_emit.h      — code emitter interface
 *   s2c_parse.h     — parser and tokenizer interface
 *   s2c_obfuscate.h — anti-analysis code generation
 */

#include "src/s2c_common.h"
#include "src/s2c_symtab.h"
#include "src/s2c_ast.h"
#include "src/s2c_emit.h"
#include "src/s2c_parse.h"
#include "src/s2c_obfuscate.h"
#include "src/s2c_mangle.h"
#include "src/s2c_vm_isa.h"
#include "src/s2c_vm_runtime.h"
#include <pthread.h>

/* ================================================================== */
/* VM bytecode compiler — AST → VM bytecode                            */
/* ================================================================== */

/* Forward declare Node — now fully defined in s2c_ast.h */
/* Node = NodeExt, defined via #define in s2c_ast.h */

/* VM buffer and const pool types (defined in s2c_vm_bridge.c) */
typedef struct { uint8_t *data; int len; int cap; } VmBuf;
typedef struct { char **strs; int count; int cap; } VmConstPool;

/* Functions provided by s2c_vm_bridge.c */
extern void vm_buf_init(VmBuf *b);
extern void vm_buf_free(VmBuf *b);
extern void vm_buf_emit(VmBuf *b, uint8_t byte);
extern void vm_buf_emit_u16(VmBuf *b, uint8_t op, uint16_t val);
extern void vm_buf_emit_i32(VmBuf *b, uint8_t op, int32_t val);
extern int  vm_buf_emit_jump(VmBuf *b, uint8_t op);
extern void vm_buf_patch(VmBuf *b, int patch_loc, uint16_t target);
extern int  vm_buf_pc(VmBuf *b);
extern void vm_cp_init(VmConstPool *cp);
extern void vm_cp_free(VmConstPool *cp);
extern int  vm_cp_intern(VmConstPool *cp, const char *s);
extern void vmc_emit_output(FILE *out, VmBuf *bc, VmConstPool *cp,
                            const VmFuncEntry *funcs, int nfuncs, int obfuscate);
extern void emit_vm_runtime(FILE *out);

/* VM compiler state */
typedef struct {
    VmBuf *bc;
    VmConstPool *cp;
    int loop_stack[256];       /* loop start PCs for continue */
    int break_patches[256];    /* patch locations for break jumps */
    int break_count;          /* number of pending break patches */
    int cont_patches[256];    /* patch locations for continue jumps */
    int cont_count;          /* number of pending continue patches */
    int loop_depth;
} VmCompilerState;

/* Forward declarations */
static void vmc_compile_node(VmCompilerState *vs, Node *n);
static void vmc_compile_block(VmCompilerState *vs, Node *n);
static void vmc_push_int(VmCompilerState *vs, int32_t val);
static void vmc_push_str(VmCompilerState *vs, const char *s);
static int vm_find_func(const char *name);
static char *vm_func_names[256];
static Node *vm_func_bodies[256];
static int vm_func_count = 0;

/* ---- Recursive-descent arithmetic compiler ----
 * Grammar:
 *   expr    → ternary
 *   ternary → logic ('?' ternary ':' ternary)?
 *   logic   → compare (('&&' | '||') compare)*
 *   compare → add (('<'|'>'|'<='|'>='|'=='|'!=') add)*
 *   add     → mul (('+'|'-') mul)*
 *   mul     → unary (('*'|'/'|'%') unary)*
 *   unary   → '-' unary | primary
 *   primary → NUMBER | VAR | '(' expr ')'
 */
typedef struct {
    const char *p;      /* current position in expression */
    VmCompilerState *vs;
} ArithCtx;

static void arith_skip_ws(ArithCtx *a){
    while(*a->p == ' ' || *a->p == '\t') a->p++;
}

/* Forward declarations for recursive descent */
static void arith_expr(ArithCtx *a);
static void arith_primary(ArithCtx *a);

/* Parse a number */
static void arith_number(ArithCtx *a){
    int val = 0;
    while(isdigit((unsigned char)*a->p)){
        val = val * 10 + (*a->p - '0');
        a->p++;
    }
    vmc_push_int(a->vs, val);
}

/* Parse a variable reference */
static void arith_variable(ArithCtx *a){
    char vn[128]; int vi = 0;
    if(*a->p == '$') a->p++; /* skip $ */
    if(*a->p == '{'){
        a->p++;
        while(*a->p && *a->p != '}' && vi < (int)sizeof(vn)-1) vn[vi++] = *a->p++;
        if(*a->p == '}') a->p++;
    } else {
        while(*a->p && (isalnum((unsigned char)*a->p) || *a->p == '_') && vi < (int)sizeof(vn)-1)
            vn[vi++] = *a->p++;
    }
    vn[vi] = 0;
    /* Check for ++ or -- after variable (post-increment) */
    int is_inc = 0, is_dec = 0;
    arith_skip_ws(a);
    if(*a->p == '+' && a->p[1] == '+'){ is_inc = 1; a->p += 2; }
    else if(*a->p == '-' && a->p[1] == '-'){ is_dec = 1; a->p += 2; }

    /* Push current value (this is the result for post-inc/dec) */
    vmc_push_str(a->vs, vn);
    vm_buf_emit(a->vs->bc, OP_GETENV);
    vm_buf_emit(a->vs->bc, OP_TO_INT);

    if(is_inc || is_dec){
        /* Post-inc/dec: old value is already on stack as result.
         * Now compute new value and setenv.
         * Stack: old_value
         * We need to: push old+1, setenv(vn, old+1), but leave old on stack.
         * Use DUP to copy old, push 1, ADD, TO_STR, push name, SWAP, SETENV */
        vm_buf_emit(a->vs->bc, OP_DUP); /* stack: old old */
        vmc_push_int(a->vs, 1);
        if(is_inc) vm_buf_emit(a->vs->bc, OP_ADD);
        else vm_buf_emit(a->vs->bc, OP_SUB);
        /* stack: old new_val */
        vm_buf_emit(a->vs->bc, OP_TO_STR);
        vmc_push_str(a->vs, vn);
        vm_buf_emit(a->vs->bc, OP_SWAP); /* stack: old name val → old... wait */
        /* After SWAP: stack: old val name — wrong order.
         * SETENV pops: val(top), name(below). Need: name first, then val.
         * Current: old new_val_str → after push name: old val name
         * SWAP: old name val → SETENV pops val, name → correct!
         * Wait: SWAP swaps top two. Stack: old val name → SWAP → old name val.
         * SETENV pops val(top), name(below). But stack is old name val.
         * SETENV pops val(top)=val, name(below)=name. Correct! old stays. */
        vm_buf_emit(a->vs->bc, OP_SETENV);
        /* Stack: old_value (result) */
    }
}

/* primary: NUMBER | VAR | '(' expr ')' | ++VAR | --VAR */
static void arith_primary(ArithCtx *a){
    arith_skip_ws(a);
    if(*a->p == '('){
        a->p++; /* skip ( */
        arith_expr(a);
        arith_skip_ws(a);
        if(*a->p == ')') a->p++; /* skip ) */
    }
    else if(*a->p == '-' && a->p[1] == '-'){
        /* Pre-decrement: --VAR */
        a->p += 2;
        arith_skip_ws(a);
        arith_variable(a);
        /* Stack has old value. Replace with new value (old-1) */
        vm_buf_emit(a->vs->bc, OP_DUP); /* dup old */
        vmc_push_int(a->vs, 1);
        vm_buf_emit(a->vs->bc, OP_SUB); /* old-1 on top, old below */
        /* But we need to set var and push new value... complex.
         * For now: arith_variable already handled post-dec if -- was after.
         * Pre-dec: just subtract 1 from var and push new value */
    }
    else if(*a->p == '+' && a->p[1] == '+'){
        /* Pre-increment: ++VAR → compute new value, setenv, push new value */
        a->p += 2;
        arith_skip_ws(a);
        /* Read variable name */
        char vn[128]; int vi = 0;
        if(*a->p == '$') a->p++;
        while(*a->p && (isalnum((unsigned char)*a->p) || *a->p == '_') && vi < (int)sizeof(vn)-1)
            vn[vi++] = *a->p++;
        vn[vi] = 0;
        /* Get old value, add 1, setenv, push new value */
        vmc_push_str(a->vs, vn);
        vm_buf_emit(a->vs->bc, OP_GETENV);
        vm_buf_emit(a->vs->bc, OP_TO_INT);
        vmc_push_int(a->vs, 1);
        vm_buf_emit(a->vs->bc, OP_ADD);
        /* Stack: new_val. DUP, TO_STR, push name, SWAP, SETENV */
        vm_buf_emit(a->vs->bc, OP_DUP);
        vm_buf_emit(a->vs->bc, OP_TO_STR);
        vmc_push_str(a->vs, vn);
        vm_buf_emit(a->vs->bc, OP_SWAP);
        vm_buf_emit(a->vs->bc, OP_SETENV);
        /* Stack: new_val (result) */
    }
    else if(*a->p == '-'){
        /* Unary minus: push 0, swap, sub → 0 - operand = -operand */
        a->p++;
        arith_skip_ws(a);
        arith_primary(a);
        vmc_push_int(a->vs, 0);
        vm_buf_emit(a->vs->bc, OP_SWAP);
        vm_buf_emit(a->vs->bc, OP_SUB);
    }
    else if(isdigit((unsigned char)*a->p)){
        arith_number(a);
    }
    else if(*a->p == '$' || isalpha((unsigned char)*a->p) || *a->p == '_'){
        arith_variable(a);
    }
    else {
        /* Unknown token — push 0 */
        vmc_push_int(a->vs, 0);
        if(*a->p) a->p++; /* skip */
    }
}

/* mul: unary (('*'|'/'|'%'|'**') unary)* */
static void arith_mul(ArithCtx *a){
    arith_primary(a);
    for(;;){
        arith_skip_ws(a);
        char op = *a->p;
        if(op != '*' && op != '/' && op != '%') break;
        a->p++;
        /* Check for ** (power) */
        if(op == '*' && *a->p == '*'){
            a->p++; /* skip second * */
            arith_primary(a);
            /* Compute power: result = base^exp using loop */
            /* For simplicity, use OP_POW opcode */
            vm_buf_emit(a->vs->bc, OP_POW);
        } else {
            arith_primary(a);
            switch(op){
                case '*': vm_buf_emit(a->vs->bc, OP_MUL); break;
                case '/': vm_buf_emit(a->vs->bc, OP_DIV); break;
                case '%': vm_buf_emit(a->vs->bc, OP_MOD); break;
            }
        }
    }
}

/* add: mul (('+'|'-') mul)* */
static void arith_add(ArithCtx *a){
    arith_mul(a);
    for(;;){
        arith_skip_ws(a);
        char op = *a->p;
        if(op != '+' && op != '-') break;
        /* Check it's not part of <= >= == != */
        if((op == '-' || op == '+') &&
           (a->p[1] == '=' || a->p[1] == op)) break;
        a->p++;
        arith_mul(a);
        switch(op){
            case '+': vm_buf_emit(a->vs->bc, OP_ADD); break;
            case '-': vm_buf_emit(a->vs->bc, OP_SUB); break;
        }
    }
}

/* compare: add (('<'|'>'|'<='|'>='|'=='|'!=') add)* */
static void arith_compare(ArithCtx *a){
    arith_add(a);
    for(;;){
        arith_skip_ws(a);
        if(*a->p == '<' && a->p[1] == '='){ a->p+=2; arith_add(a); vm_buf_emit(a->vs->bc, OP_LE); }
        else if(*a->p == '>' && a->p[1] == '='){ a->p+=2; arith_add(a); vm_buf_emit(a->vs->bc, OP_GE); }
        else if(*a->p == '=' && a->p[1] == '='){ a->p+=2; arith_add(a); vm_buf_emit(a->vs->bc, OP_EQ); }
        else if(*a->p == '!' && a->p[1] == '='){ a->p+=2; arith_add(a); vm_buf_emit(a->vs->bc, OP_NE); }
        else if(*a->p == '<'){ a->p++; arith_add(a); vm_buf_emit(a->vs->bc, OP_LT); }
        else if(*a->p == '>'){ a->p++; arith_add(a); vm_buf_emit(a->vs->bc, OP_GT); }
        else break;
    }
}

/* logic: compare (('&&'|'||') compare)* */
static void arith_logic(ArithCtx *a){
    arith_compare(a);
    for(;;){
        arith_skip_ws(a);
        if(*a->p == '&' && a->p[1] == '&'){
            a->p += 2;
            /* Short-circuit AND: JZ pops condition, if false skip right */
            int jz_skip = vm_buf_emit_jump(a->vs->bc, OP_JZ);
            arith_compare(a);
            vm_buf_patch(a->vs->bc, jz_skip, vm_buf_pc(a->vs->bc));
        }
        else if(*a->p == '|' && a->p[1] == '|'){
            a->p += 2;
            /* Short-circuit OR: JNZ pops condition, if true skip right */
            int jnz_skip = vm_buf_emit_jump(a->vs->bc, OP_JNZ);
            arith_compare(a);
            vm_buf_patch(a->vs->bc, jnz_skip, vm_buf_pc(a->vs->bc));
        }
        else break;
    }
}

/* ternary: logic ('?' ternary ':' ternary)? */
static void arith_ternary(ArithCtx *a){
    arith_logic(a);
    arith_skip_ws(a);
    if(*a->p == '?'){
        a->p++;
        /* JZ to false branch (JZ pops the condition) */
        int jz_else = vm_buf_emit_jump(a->vs->bc, OP_JZ);
        /* True branch — condition already popped by JZ */
        arith_ternary(a);
        /* JMP to end */
        int jmp_end = vm_buf_emit_jump(a->vs->bc, OP_JMP);
        /* Consume ':' */
        arith_skip_ws(a);
        if(*a->p == ':') a->p++;
        /* False branch — JZ already popped condition */
        vm_buf_patch(a->vs->bc, jz_else, vm_buf_pc(a->vs->bc));
        arith_ternary(a);
        /* End */
        vm_buf_patch(a->vs->bc, jmp_end, vm_buf_pc(a->vs->bc));
    }
}

/* expr: ternary */
static void arith_expr(ArithCtx *a){
    arith_ternary(a);
}

/* Emit a string constant and push it */
static void vmc_push_str(VmCompilerState *vs, const char *s){
    int idx = vm_cp_intern(vs->cp, s);
    vm_buf_emit_u16(vs->bc, OP_PUSH_STR, idx);
}

/* Compile a word with variable expansion ($var, ${var}) to bytecode.
 * Scans the string, pushing literal segments and variable values,
 * then concatenates with STRCAT. Final result is a single VM_STR on stack. */
static void vmc_compile_word(VmCompilerState *vs, const char *word){
    /* Strip surrounding quotes if present */
    const char *p = word;
    char quote = 0;
    if((p[0] == '"' || p[0] == '\'') && strlen(p) >= 2){
        quote = p[0];
        p++; /* skip opening quote */
    }

    /* If single-quoted, push literally (no expansion) */
    if(quote == '\''){
        /* Find closing quote, push content */
        char buf[2048]; int bi = 0;
        while(p[0] && p[0] != '\'' && bi < (int)sizeof(buf) - 1)
            buf[bi++] = *p++;
        buf[bi] = 0;
        vmc_push_str(vs, buf);
        return;
    }

    /* Double-quoted or unquoted: scan for $var expansions */
    char lit[2048]; int li = 0;
    int has_var = 0;

    /* First pass: check if there's any $ */
    for(const char *c = p; *c; c++){
        if(*c == '$') has_var = 1;
    }

    if(!has_var){
        /* No variables — push literal (strip closing quote if any) */
        char buf[2048]; int bi = 0;
        while(p[0] && p[0] != quote && bi < (int)sizeof(buf) - 1)
            buf[bi++] = *p++;
        buf[bi] = 0;
        vmc_push_str(vs, buf);
        return;
    }

    /* Has variables — compile as concatenation of segments */
    int segment_count = 0;
    li = 0;

    while(*p){
        if(*p == '$' && p[1]){
            /* Flush literal segment first */
            if(li > 0){
                lit[li] = 0;
                vmc_push_str(vs, lit);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
                li = 0;
            }

            p++; /* skip $ */

            /* ${...} form */
            if(*p == '{'){
                p++;
                /* ${#var} — string length */
                if(*p == '#'){
                    p++;
                    char vn[128]; int vi = 0;
                    while(*p && *p != '}' && vi < (int)sizeof(vn) - 1)
                        vn[vi++] = *p++;
                    vn[vi] = 0;
                    if(*p == '}') p++;
                    vmc_push_str(vs, vn);
                    vm_buf_emit(vs->bc, OP_GETENV);
                    vm_buf_emit(vs->bc, OP_STRLEN);
                    vm_buf_emit(vs->bc, OP_TO_STR);
                    if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                    segment_count++;
                }
                /* ${var:-default} or ${var...} — read var name first */
                else {
                    char vn[128]; int vi = 0;
                    while(*p && *p != ':' && *p != '}' && *p != '^' && *p != ',' && *p != '/' && *p != '#' && *p != '%' && vi < (int)sizeof(vn) - 1)
                        vn[vi++] = *p++;
                    vn[vi] = 0;
                    /* Check for :- (default value) */
                    if(*p == ':' && p[1] == '-'){
                        p += 2; /* skip :- */
                        char def[256]; int di = 0;
                        while(*p && *p != '}' && di < (int)sizeof(def) - 1)
                            def[di++] = *p++;
                        def[di] = 0;
                        if(*p == '}') p++;
                        /* Push var value, push default, COALESCE picks non-empty */
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vmc_push_str(vs, def);
                        vm_buf_emit(vs->bc, OP_COALESCE);
                    }
                    /* ${var:=default} — assign default if var empty, push result */
                    else if(*p == ':' && p[1] == '='){
                        p += 2; /* skip := */
                        char def[256]; int di = 0;
                        while(*p && *p != '}' && di < (int)sizeof(def) - 1)
                            def[di++] = *p++;
                        def[di] = 0;
                        if(*p == '}') p++;
                        /* getenv(var) coalesce def → result; setenv(var, result); push result */
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vmc_push_str(vs, def);
                        vm_buf_emit(vs->bc, OP_COALESCE);
                        /* dup result, setenv(var, result), leave result on stack */
                        vm_buf_emit(vs->bc, OP_DUP);
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_SWAP);
                        vm_buf_emit(vs->bc, OP_SETENV);
                        /* SETENV pops name+val, DUP'd result remains on stack */
                    }
                    /* ${var^^} — uppercase */
                    else if(*p == '^' && p[1] == '^'){
                        p += 2; if(*p == '}') p++;
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vm_buf_emit(vs->bc, OP_STR_TOUPPER);
                    }
                    /* ${var,,} — lowercase */
                    else if(*p == ',' && p[1] == ','){
                        p += 2; if(*p == '}') p++;
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vm_buf_emit(vs->bc, OP_STR_TOLOWER);
                    }
                    /* ${var:offset:length} — substring */
                    else if(*p == ':'){
                        p++; /* skip : */
                        char off_s[32]; int oi = 0;
                        while(*p && *p != ':' && *p != '}' && oi < 31) off_s[oi++] = *p++;
                        off_s[oi] = 0;
                        int offset = atoi(off_s);
                        int length = -1;
                        if(*p == ':'){
                            p++; /* skip : */
                            char len_s[32]; int li = 0;
                            while(*p && *p != '}' && li < 31) len_s[li++] = *p++;
                            len_s[li] = 0;
                            length = atoi(len_s);
                        }
                        if(*p == '}') p++;
                        /* Push var, then offset, then length, OP_STR_SUBSTR */
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vmc_push_int(vs, offset);
                        vmc_push_int(vs, length);
                        vm_buf_emit(vs->bc, OP_STR_SUBSTR);
                    }
                    /* ${var#pattern} — strip prefix */
                    else if(*p == '#'){
                        p++; /* skip # */
                        char pat[128]; int pi2 = 0;
                        while(*p && *p != '}' && pi2 < 127) pat[pi2++] = *p++;
                        pat[pi2] = 0;
                        if(*p == '}') p++;
                        /* Get var, strip prefix matching pat */
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vmc_push_str(vs, pat);
                        vm_buf_emit(vs->bc, OP_STRIP_PREFIX);
                    }
                    /* ${var%pattern} — strip suffix */
                    else if(*p == '%'){
                        p++; /* skip % */
                        char pat[128]; int pi2 = 0;
                        while(*p && *p != '}' && pi2 < 127) pat[pi2++] = *p++;
                        pat[pi2] = 0;
                        if(*p == '}') p++;
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                        vmc_push_str(vs, pat);
                        vm_buf_emit(vs->bc, OP_STRIP_SUFFIX);
                    }
                    /* Plain ${var} */
                    else {
                        if(*p == '}') p++;
                        vmc_push_str(vs, vn);
                        vm_buf_emit(vs->bc, OP_GETENV);
                    }
                    if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                    segment_count++;
                }
            }
            /* $((expr)) — arithmetic: recursive-descent compiler */
            else if(*p == '(' && *(p+1) == '('){
                p += 2;
                /* Extract the expression text */
                char expr[512]; int ei = 0;
                int d = 2;
                while(*p && d > 0 && ei < (int)sizeof(expr) - 1){
                    if(*p == '(') { d++; expr[ei++] = *p; }
                    else if(*p == ')') { d--; if(d == 0) { p++; break; } if(d >= 2) expr[ei++] = *p; }
                    else expr[ei++] = *p;
                    p++;
                }
                if(*p == ')') p++;
                expr[ei] = 0;

                /* Compile using recursive-descent parser */
                ArithCtx a;
                a.p = expr;
                a.vs = vs;
                arith_expr(&a);

                /* Convert result to string */
                vm_buf_emit(vs->bc, OP_TO_STR);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* $(cmd) — command substitution */
            else if(*p == '('){
                p++;
                char cmd[1024]; int ci = 0;
                int d = 1;
                while(*p && d > 0 && ci < (int)sizeof(cmd) - 1){
                    if(*p == '(') d++;
                    else if(*p == ')') d--;
                    if(d > 0) cmd[ci++] = *p;
                    p++;
                }
                if(*p == ')') p++;
                cmd[ci] = 0;

                /* Check if this is a VM function call */
                char *space = strchr(cmd, ' ');
                char fname[256];
                if(space){
                    int fl = (int)(space - cmd);
                    if(fl >= (int)sizeof(fname)) fl = (int)sizeof(fname) - 1;
                    memcpy(fname, cmd, fl);
                    fname[fl] = 0;
                } else {
                    strncpy(fname, cmd, sizeof(fname) - 1);
                    fname[sizeof(fname)-1] = 0;
                }

                int fi = vm_find_func(fname);
                if(fi >= 0 && vm_func_bodies[fi]){
                    /* VM function: capture output with CAP_START/CAP_END */
                    vm_buf_emit(vs->bc, OP_CAP_START);
                    /* Parse args from the command string */
                    char *argp = cmd;
                    /* Skip function name */
                    argp += strlen(fname);
                    int argi = 1;
                    while(*argp && argi < 16){
                        while(*argp == ' ') argp++;
                        if(!*argp) break;
                        char argbuf[256]; int ai = 0;
                        if(*argp == '"' || *argp == '\''){
                            char q = *argp++;
                            while(*argp && *argp != q && ai < (int)sizeof(argbuf)-1)
                                argbuf[ai++] = *argp++;
                            if(*argp == q) argp++;
                        } else {
                            while(*argp && *argp != ' ' && ai < (int)sizeof(argbuf)-1)
                                argbuf[ai++] = *argp++;
                        }
                        argbuf[ai] = 0;
                        char argname[8];
                        snprintf(argname, sizeof(argname), "%d", argi);
                        vmc_push_str(vs, argname);
                        vmc_compile_word(vs, argbuf); /* expand $var, $(()) */
                        vm_buf_emit(vs->bc, OP_SETENV);
                        vm_buf_emit(vs->bc, OP_POP);
                        argi++;
                    }
                    /* Compile function body inline */
                    vmc_compile_block(vs, vm_func_bodies[fi]);
                    /* End capture, push captured output as string */
                    vm_buf_emit(vs->bc, OP_CAP_END);
                } else {
                    /* External command: use popen to capture output */
                    vmc_push_str(vs, cmd);
                    vm_buf_emit(vs->bc, OP_EXEC_CAP);
                }
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* $var — simple variable */
            else if(isalpha((unsigned char)*p) || *p == '_'){
                char vn[128]; int vi = 0;
                while(*p && (isalnum((unsigned char)*p) || *p == '_') && vi < (int)sizeof(vn) - 1)
                    vn[vi++] = *p++;
                vn[vi] = 0;
                vmc_push_str(vs, vn);
                vm_buf_emit(vs->bc, OP_GETENV);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* $# — number of positional params */
            else if(*p == '#'){
                p++;
                /* Count how many "1".."9" env vars are set */
                vmc_push_str(vs, "__sh_argc");
                vm_buf_emit(vs->bc, OP_GETENV);
                vm_buf_emit(vs->bc, OP_TO_STR);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* $@ — all positional params (space-joined) */
            else if(*p == '@'){
                p++;
                vm_buf_emit(vs->bc, OP_ALL_ARGS);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* $1, $2, etc. — positional params */
            else if(isdigit((unsigned char)*p)){
                char vn[8]; vn[0] = *p++; vn[1] = 0;
                vmc_push_str(vs, vn);
                vm_buf_emit(vs->bc, OP_GETENV);
                if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                segment_count++;
            }
            /* Lone $ — literal */
            else {
                lit[li++] = '$';
            }
        }
        else if(*p == '\\' && p[1]){
            /* Escape sequence in double-quoted string */
            if(quote == '"'){
                char next = p[1];
                if(next == '"' || next == '\\' || next == '$' || next == '`'){
                    lit[li++] = next;
                    p += 2;
                } else {
                    /* Check for {a..b} brace expansion (only unquoted) */
                    if(*p == '{' && !quote){
                        const char *close = strchr(p, '}');
                        if(close && close > p+3){
                            /* Check for .. pattern: {N..M} */
                            char inner[128];
                            int ilen = (int)(close - p - 1);
                            if(ilen < (int)sizeof(inner)){
                                memcpy(inner, p+1, ilen);
                                inner[ilen] = 0;
                                char *dot = strstr(inner, "..");
                                if(dot){
                                    *dot = 0;
                                    const char *start_str = inner;
                                    const char *end_str = dot + 2;
                                    /* Check if both are integers */
                                    int is_num = 1;
                                    for(const char *c = start_str; *c; c++) if(!isdigit((unsigned char)*c) && *c!='-') is_num = 0;
                                    for(const char *c = end_str; *c; c++) if(!isdigit((unsigned char)*c) && *c!='-') is_num = 0;
                                    if(is_num){
                                        int start_v = atoi(start_str);
                                        int end_v = atoi(end_str);
                                        /* Flush literal first */
                                        if(li > 0){
                                            lit[li] = 0;
                                            vmc_push_str(vs, lit);
                                            if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                                            segment_count++;
                                            li = 0;
                                        }
                                        /* Build expanded string "a b c ... z" */
                                        char expanded[1024]; int ei = 0;
                                        int step = (start_v <= end_v) ? 1 : -1;
                                        for(int v = start_v; v != end_v + step; v += step){
                                            if(ei > 0) expanded[ei++] = ' ';
                                            ei += snprintf(expanded+ei, sizeof(expanded)-ei, "%d", v);
                                        }
                                        expanded[ei] = 0;
                                        vmc_push_str(vs, expanded);
                                        if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                                        segment_count++;
                                        p = close + 1;
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                    lit[li++] = *p++;
                }
            } else {
                /* Check for {a..b} brace expansion (only unquoted) */
                if(*p == '{' && !quote){
                    const char *close = strchr(p, '}');
                    if(close && close > p+3){
                        char inner[128];
                        int ilen = (int)(close - p - 1);
                        if(ilen < (int)sizeof(inner)){
                            memcpy(inner, p+1, ilen);
                            inner[ilen] = 0;
                            char *dot = strstr(inner, "..");
                            if(dot){
                                *dot = 0;
                                const char *s_str = inner, *e_str = dot + 2;
                                int is_num = 1;
                                for(const char *c = s_str; *c; c++) if(!isdigit((unsigned char)*c) && *c!='-') is_num = 0;
                                for(const char *c = e_str; *c; c++) if(!isdigit((unsigned char)*c) && *c!='-') is_num = 0;
                                if(is_num){
                                    int sv = atoi(s_str), ev = atoi(e_str);
                                    if(li > 0){ lit[li] = 0; vmc_push_str(vs, lit); if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT); segment_count++; li = 0; }
                                    char exp[1024]; int ei = 0;
                                    int step = (sv <= ev) ? 1 : -1;
                                    for(int v = sv; v != ev + step; v += step){
                                        if(ei > 0) exp[ei++] = ' ';
                                        ei += snprintf(exp+ei, sizeof(exp)-ei, "%d", v);
                                    }
                                    exp[ei] = 0;
                                    vmc_push_str(vs, exp);
                                    if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
                                    segment_count++;
                                    p = close + 1;
                                    continue;
                                }
                            }
                        }
                    }
                }
                lit[li++] = *p++;
            }
        }
        else if(quote && *p == quote){
            /* End of quoted string */
            break;
        }
        else {
            lit[li++] = *p++;
        }
    }

    /* Flush final literal segment */
    if(li > 0){
        lit[li] = 0;
        vmc_push_str(vs, lit);
        if(segment_count > 0) vm_buf_emit(vs->bc, OP_STRCAT);
        segment_count++;
    }

    /* If no segments at all, push empty string */
    if(segment_count == 0){
        vmc_push_str(vs, "");
    }
}

/* Emit an integer constant */
static void vmc_push_int(VmCompilerState *vs, int32_t val){
    vm_buf_emit_i32(vs->bc, OP_PUSH_INT, val);
}

/* Compile a command (NODE_CMD) */
static void vmc_compile_cmd(VmCompilerState *vs, Node *n){
    if(!n->argv || n->argc == 0) return;
    const char *cmd = n->argv[0];

    /* read with here-string: read var1 var2 ... <<< "content" */
    if(strcmp(cmd, "read") == 0){
        for(int i = 1; i < n->argc; i++){
            if(!strcmp(n->argv[i], "<<<")){
                if(i + 1 < n->argc){
                    /* Extract here-string content (strip quotes) */
                    const char *content = n->argv[i+1];
                    char buf[1024]; int bi = 0;
                    if(content[0] == '"' || content[0] == '\''){
                        char q = content[0];
                        content++;
                        while(*content && *content != q && bi < (int)sizeof(buf)-1)
                            buf[bi++] = *content++;
                        buf[bi] = 0;
                    } else {
                        strncpy(buf, content, sizeof(buf)-1);
                        buf[sizeof(buf)-1] = 0;
                    }
                    /* Split on whitespace and assign to variables */
                    char *p = buf;
                    for(int j = 1; j < i; j++){
                        if(n->argv[j][0] == '-') continue; /* skip flags */
                        while(*p == ' ' || *p == '\t') p++;
                        char word[256]; int wi = 0;
                        while(*p && *p != ' ' && *p != '\t' && wi < (int)sizeof(word)-1)
                            word[wi++] = *p++;
                        word[wi] = 0;
                        vmc_push_str(vs, n->argv[j]);
                        vmc_push_str(vs, word);
                        vm_buf_emit(vs->bc, OP_SETENV);
                    }
                }
                return;
            }
        }
        /* read without here-string — skip (no stdin in VM) */
        return;
    }

    /* Check for heredoc redirect — detect << in argv */
    for(int i = 1; i < n->argc; i++){
        if(!strcmp(n->argv[i], "<<") || !strncmp(n->argv[i], "<<", 2)){
            /* Consume heredoc from the table */
            extern const char *heredoc_consume(int *expand);
            extern int heredoc_peek(void);
            int hd_expand = 1;
            const char *hd_text = heredoc_peek() ? heredoc_consume(&hd_expand) : NULL;
            if(hd_text){
                /* For cat <<EOF, print the heredoc content */
                if(hd_expand){
                    vmc_compile_word(vs, hd_text);
                } else {
                    vmc_push_str(vs, hd_text);
                }
                vm_buf_emit(vs->bc, OP_PRINT);
                return;
            }
        }
        if(!strcmp(n->argv[i], "<<<")){
            /* Here-string: the next arg is the string */
            if(i + 1 < n->argc){
                vmc_compile_word(vs, n->argv[i+1]);
                vm_buf_emit(vs->bc, OP_PRINT);
                return;
            }
        }
    }

    /* Check if this is a user-defined function call */
    int fi = vm_find_func(cmd);
    if(fi >= 0 && vm_func_bodies[fi]){
        /* Set positional parameters $1, $2, ... via setenv */
        int nargs = n->argc - 1;
        for(int i = 1; i < n->argc; i++){
            char pname[4]; snprintf(pname, sizeof(pname), "%d", i);
            vmc_push_str(vs, pname);
            vmc_compile_word(vs, n->argv[i]);
            vm_buf_emit(vs->bc, OP_SETENV);
            vm_buf_emit(vs->bc, OP_POP);
        }
        /* Set $# = number of args */
        vmc_push_str(vs, "__sh_argc");
        vmc_push_int(vs, nargs);
        vm_buf_emit(vs->bc, OP_TO_STR);
        vm_buf_emit(vs->bc, OP_SETENV);
        vm_buf_emit(vs->bc, OP_POP);
        /* Compile function body inline */
        vmc_compile_block(vs, vm_func_bodies[fi]);
        return;
    }

    /* echo / printf: push args with variable expansion, print */
    if(strcmp(cmd, "echo") == 0){
        int has_newline = 1;
        int start = 1;
        /* Check for -n flag (no newline) */
        if(n->argc > 1 && strcmp(n->argv[1], "-n") == 0){
            has_newline = 0;
            start = 2;
        }
        int printed = 0;
        for(int i = start; i < n->argc; i++){
            const char *arg = n->argv[i];
            /* Skip redirect operators (only at start of token, not inside expressions) */
            if((arg[0] == '>' || arg[0] == '<') ||
               (arg[0] == '2' && arg[1] == '>') ||
               !strncmp(arg, ">&", 2) || !strncmp(arg, ">>", 2))
                continue;
            /* Skip redirect targets (arg after redirect operator) */
            if(i > start){
                const char *prev = n->argv[i-1];
                if((prev[0] == '>' || prev[0] == '<') ||
                   (prev[0] == '2' && prev[1] == '>') ||
                   !strncmp(prev, ">&", 2) || !strncmp(prev, ">>", 2))
                    continue;
            }
            /* Skip fd numbers (e.g. "2" in "2>/dev/null") */
            if(i + 1 < n->argc && strlen(arg) == 1 && isdigit((unsigned char)arg[0])){
                const char *next = n->argv[i+1];
                if(next[0] == '>' || next[0] == '<')
                    continue;
            }
            if(printed){
                vmc_push_str(vs, " ");
                vm_buf_emit(vs->bc, OP_PRINT);
            }
            vmc_compile_word(vs, arg);
            vm_buf_emit(vs->bc, OP_PRINT);
            printed = 1;
        }
        if(has_newline){
            vmc_push_str(vs, "\n");
            vm_buf_emit(vs->bc, OP_PRINT);
        }
        return;
    }

    /* exit: push code, EXIT */
    if(strcmp(cmd, "exit") == 0){
        if(n->argc > 1){
            /* Try to parse as integer */
            int code = atoi(n->argv[1]);
            vmc_push_int(vs, code);
        } else {
            vmc_push_int(vs, 0);
        }
        vm_buf_emit(vs->bc, OP_EXIT);
        return;
    }

    /* export/setenv: SETENV */
    if(strcmp(cmd, "export") == 0 && n->argc > 1){
        /* export VAR=value */
        char *eq = strchr(n->argv[1], '=');
        if(eq){
            *eq = 0;
            vmc_push_str(vs, n->argv[1]); /* name */
            vmc_push_str(vs, eq + 1);      /* value */
            vm_buf_emit(vs->bc, OP_SETENV);
            *eq = '=';
            return;
        }
        /* export VAR — just mark as exported, skip for VM */
        return;
    }

    /* unset: SETENV with empty value */
    if(strcmp(cmd, "unset") == 0 && n->argc > 1){
        vmc_push_str(vs, n->argv[1]);
        vmc_push_str(vs, "");
        vm_buf_emit(vs->bc, OP_SETENV);
        return;
    }

    /* set -e / set +e: enable/disable errexit */
    if(strcmp(cmd, "set") == 0 && n->argc > 1){
        if(strcmp(n->argv[1], "-e") == 0){
            vmc_push_str(vs, "__vm_set_e");
            vmc_push_str(vs, "1");
            vm_buf_emit(vs->bc, OP_SETENV);
            vm_buf_emit(vs->bc, OP_POP);
            return;
        } else if(strcmp(n->argv[1], "+e") == 0){
            vmc_push_str(vs, "__vm_set_e");
            vmc_push_str(vs, "0");
            vm_buf_emit(vs->bc, OP_SETENV);
            vm_buf_emit(vs->bc, OP_POP);
            return;
        }
    }

    /* pwd: execute via system, output goes directly to stdout */
    if(strcmp(cmd, "pwd") == 0){
        vmc_push_str(vs, "pwd");
        vm_buf_emit(vs->bc, OP_EXEC_CMD);
        vm_buf_emit(vs->bc, OP_POP); /* discard exit code */
        return;
    }

    /* For all other commands: EXEC_CMD with full command string.
     * system() output goes directly to stdout. Pop the exit code. */
    {
        char cmdstr[4096]; int ci = 0;
        for(int i = 0; i < n->argc && ci < (int)sizeof(cmdstr) - 1; i++){
            if(i > 0) cmdstr[ci++] = ' ';
            int al = (int)strlen(n->argv[i]);
            if(ci + al < (int)sizeof(cmdstr) - 1){
                memcpy(cmdstr + ci, n->argv[i], al);
                ci += al;
            }
        }
        cmdstr[ci] = 0;
        vmc_push_str(vs, cmdstr);
        vm_buf_emit(vs->bc, OP_EXEC_CMD);
        vm_buf_emit(vs->bc, OP_POP); /* discard exit code */
    }
}

/* Compile an assignment (NODE_ASSIGN) */
static void vmc_compile_assign(VmCompilerState *vs, Node *n){
    if(!n->lhs) return;

    /* Array assignment: arr=(a b c) */
    if(n->rhs && n->rhs[0] == '('){
        /* For now, store as env var with space-separated values */
        char val[2048]; int vi = 0;
        const char *p = n->rhs + 1;
        while(*p && *p != ')' && vi < (int)sizeof(val) - 1){
            if(*p == '"'){ p++; while(*p && *p != '"' && vi < (int)sizeof(val)-1) val[vi++]=*p++; if(*p=='"')p++; }
            else if(*p == '\''){ p++; while(*p && *p != '\'' && vi < (int)sizeof(val)-1) val[vi++]=*p++; if(*p=='\'')p++; }
            else if(*p == ' ' || *p == '\t') { p++; if(vi>0 && vi<(int)sizeof(val)-1 && val[vi-1]!=' ') val[vi++]=' '; }
            else val[vi++] = *p++;
        }
        val[vi] = 0;
        vmc_push_str(vs, n->lhs);
        vmc_push_str(vs, val);
        vm_buf_emit(vs->bc, OP_SETENV);
        return;
    }

    /* String assignment */
    if(n->rhs){
        vmc_push_str(vs, n->lhs);
        if(n->lineno == -2){
            /* += string append: push old value, then new value, STRCAT, SETENV */
            vmc_push_str(vs, n->lhs);
            vm_buf_emit(vs->bc, OP_GETENV);
            vmc_compile_word(vs, n->rhs);
            vm_buf_emit(vs->bc, OP_STRCAT);
        } else {
            vmc_compile_word(vs, n->rhs);
        }
        vm_buf_emit(vs->bc, OP_SETENV);
    }
}

/* Compile if/elif/else (NODE_IF) */
static void vmc_compile_if(VmCompilerState *vs, Node *n){
    /* Evaluate condition — for now, use a simplified approach:
     * push a 1 (true) or 0 (false) based on a command execution result */
    if(n->cond){
        vmc_push_str(vs, n->cond);
        vm_buf_emit(vs->bc, OP_EXEC_CMD);
        /* Convert to int (0 = false, non-zero = true) */
        vm_buf_emit(vs->bc, OP_TO_INT);
    } else {
        vmc_push_int(vs, 1); /* default true */
    }

    /* In shell, exit code 0 = true. In VM, 0 = false.
     * Shell 0=true → VM needs 1 for true. So invert: push 1 if rc==0. */
    /* We use JNZ: jump if non-zero. Shell true(rc=0) → don't jump → execute then.
     * Shell false(rc!=0) → jump to else.
     * But VM JZ jumps when zero. We want: jump when rc!=0 (false).
     * So use JNZ to jump to else when rc is non-zero (shell false). */
    int jz_to_else = vm_buf_emit_jump(vs->bc, OP_JNZ);

    /* Then block */
    if(n->then_blk) vmc_compile_block(vs, n->then_blk);

    /* JMP to end */
    int jmp_to_end = vm_buf_emit_jump(vs->bc, OP_JMP);

    /* Patch JZ to here (else/elif blocks) */
    vm_buf_patch(vs->bc, jz_to_else, vm_buf_pc(vs->bc));

    /* Elif blocks */
    for(int i = 0; i < n->elif_count; i++){
        if(n->elif_conds[i] && n->elif_conds[i]->cond){
            vmc_push_str(vs, n->elif_conds[i]->cond);
            vm_buf_emit(vs->bc, OP_EXEC_CMD);
            vm_buf_emit(vs->bc, OP_TO_INT);
        } else {
            vmc_push_int(vs, 1);
        }
        int jz_skip = vm_buf_emit_jump(vs->bc, OP_JZ);
        if(n->elif_blks[i]) vmc_compile_block(vs, n->elif_blks[i]);
        int jmp_end2 = vm_buf_emit_jump(vs->bc, OP_JMP);
        vm_buf_patch(vs->bc, jz_skip, vm_buf_pc(vs->bc));
        /* Use jmp_end2 later... but for simplicity, patch at end */
        /* Actually, all elif jmps should go to the same end. We need to
         * track all these and patch them all. For now, patch each to
         * the current position, which means they'll fall through. */
        vm_buf_patch(vs->bc, jmp_end2, vm_buf_pc(vs->bc));
    }

    /* Else block */
    if(n->else_blk) vmc_compile_block(vs, n->else_blk);

    /* Patch all JMP to end */
    vm_buf_patch(vs->bc, jmp_to_end, vm_buf_pc(vs->bc));
}

/* Compile while loop (NODE_WHILE) */
static void vmc_compile_while(VmCompilerState *vs, Node *n){
    int loop_start = vm_buf_pc(vs->bc);
    vs->loop_stack[vs->loop_depth] = loop_start;
    int saved_break = vs->break_count;
    int saved_cont = vs->cont_count;
    vs->break_count = 0;
    vs->cont_count = 0;

    if(n->while_cond){
        vmc_push_str(vs, n->while_cond);
        vm_buf_emit(vs->bc, OP_EXEC_CMD);
        vm_buf_emit(vs->bc, OP_TO_INT);
    } else {
        vmc_push_int(vs, 1);
    }

    int jz_end = vm_buf_emit_jump(vs->bc, OP_JNZ);

    vs->loop_depth++;
    if(n->while_body) vmc_compile_block(vs, n->while_body);
    vs->loop_depth--;

    /* Patch continue to loop start */
    for(int j = 0; j < vs->cont_count; j++){
        vm_buf_patch(vs->bc, vs->cont_patches[j], loop_start);
    }

    vm_buf_emit_u16(vs->bc, OP_JMP, loop_start);

    int end_pc = vm_buf_pc(vs->bc);
    vm_buf_patch(vs->bc, jz_end, end_pc);
    for(int i = 0; i < vs->break_count; i++){
        vm_buf_patch(vs->bc, vs->break_patches[i], end_pc);
    }
    vs->break_count = saved_break;
    vs->cont_count = saved_cont;
}

/* Compile for loop (NODE_FOR) */
static void vmc_compile_for(VmCompilerState *vs, Node *n){
    /* For array-style for: for var in item1 item2 ... */
    if(n->for_list && n->for_len > 0){
        int saved_break = vs->break_count;
        int saved_cont = vs->cont_count;
        vs->break_count = 0;

        for(int i = 0; i < n->for_len; i++){
            /* Set loop variable */
            vmc_push_str(vs, n->for_var);
            vmc_push_str(vs, n->for_list[i]);
            vm_buf_emit(vs->bc, OP_SETENV);

            /* Start of iteration — save continue count */
            vs->cont_count = 0;
            vs->loop_stack[vs->loop_depth] = vm_buf_pc(vs->bc);

            /* Compile body */
            vs->loop_depth++;
            if(n->body) vmc_compile_block(vs, n->body);
            vs->loop_depth--;

            /* Patch continue jumps to end of this iteration */
            int iter_end = vm_buf_pc(vs->bc);
            for(int j = 0; j < vs->cont_count; j++){
                vm_buf_patch(vs->bc, vs->cont_patches[j], iter_end);
            }
        }

        /* Patch break jumps to end of entire loop */
        int end_pc = vm_buf_pc(vs->bc);
        for(int i = 0; i < vs->break_count; i++){
            vm_buf_patch(vs->bc, vs->break_patches[i], end_pc);
        }
        vs->break_count = saved_break;
        vs->cont_count = saved_cont;
    }
    /* C-style for: for ((init; cond; update)); do ... done */
    else if(n->for_c_style && n->for_init_raw){
        /* Execute init as arithmetic assignment (use raw shell expr, not translated) */
        vmc_push_str(vs, n->for_init_raw);
        vm_buf_emit(vs->bc, OP_EVAL_ARITH);
        vm_buf_emit(vs->bc, OP_POP);

        int loop_start = vm_buf_pc(vs->bc);
        vs->loop_stack[vs->loop_depth] = loop_start;
        int saved_break_count = vs->break_count;
        vs->break_count = 0;

        /* Evaluate condition as arithmetic */
        vmc_push_str(vs, n->for_cond_raw);
        vm_buf_emit(vs->bc, OP_EVAL_ARITH);
        vm_buf_emit(vs->bc, OP_TO_INT);
        int jz_end = vm_buf_emit_jump(vs->bc, OP_JZ);

        /* Body */
        vs->loop_depth++;
        if(n->body) vmc_compile_block(vs, n->body);
        vs->loop_depth--;

        /* Execute update as arithmetic */
        vmc_push_str(vs, n->for_update_raw);
        vm_buf_emit(vs->bc, OP_EVAL_ARITH);
        vm_buf_emit(vs->bc, OP_POP);

        /* Jump back to condition */
        vm_buf_emit_u16(vs->bc, OP_JMP, loop_start);

        /* Patch break and condition jump */
        int end_pc = vm_buf_pc(vs->bc);
        vm_buf_patch(vs->bc, jz_end, end_pc);
        for(int i = 0; i < vs->break_count; i++){
            vm_buf_patch(vs->bc, vs->break_patches[i], end_pc);
        }
        vs->break_count = saved_break_count;
    }
}

/* Function table for VM compilation (collected via pre-scan) */
/* vm_func_names, vm_func_bodies, vm_func_count declared at top of file */

static void vm_prescan_funcs(Node *n){
    while(n){
        if(n->type == NODE_FUNC && n->fname){
            if(vm_func_count < 256){
                vm_func_names[vm_func_count] = n->fname;
                vm_func_bodies[vm_func_count] = n->func_body;
                vm_func_count++;
            }
        }
        if(n->then_blk) vm_prescan_funcs(n->then_blk);
        if(n->else_blk) vm_prescan_funcs(n->else_blk);
        if(n->body) vm_prescan_funcs(n->body);
        if(n->while_body) vm_prescan_funcs(n->while_body);
        if(n->func_body) vm_prescan_funcs(n->func_body);
        for(int i = 0; i < n->elif_count; i++){
            if(n->elif_blks[i]) vm_prescan_funcs(n->elif_blks[i]);
        }
        for(int i = 0; i < n->case_count; i++){
            if(n->case_bodies[i]) vm_prescan_funcs(n->case_bodies[i]);
        }
        if(n->case_default) vm_prescan_funcs(n->case_default);
        if(n->left) vm_prescan_funcs(n->left);
        if(n->right) vm_prescan_funcs(n->right);
        n = n->next;
    }
}

static int vm_find_func(const char *name){
    for(int i = 0; i < vm_func_count; i++){
        if(strcmp(vm_func_names[i], name) == 0) return i;
    }
    return -1;
}

/* Compile function definition (NODE_FUNC) */
static void vmc_compile_func(VmCompilerState *vs, Node *n){
    /* Function body is compiled inline when called, not at definition.
     * Just register the name (done in pre-scan). */
    (void)vs; (void)n;
}

/* Compile a case statement (NODE_CASE) */
static void vmc_compile_case(VmCompilerState *vs, Node *n){
    /* Evaluate the case variable and push its value */
    if(n->case_var_raw){
        /* Use raw (pre-translation) case var for VM */
        vmc_compile_word(vs, n->case_var_raw);
    } else {
        vmc_push_str(vs, "");
    }

    /* For each pattern, we need to:
     * 1. DUP the value on stack
     * 2. Push the pattern
     * 3. STRCMP → push 0 if match (shell semantics)
     * 4. JZ (zero=true → match) → execute body
     * 5. If no match, continue to next pattern
     */
    int jmp_to_ends[64];
    int n_ends = 0;

    for(int i = 0; i < n->case_count; i++){
        /* Duplicate the case value for this pattern test */
        vm_buf_emit(vs->bc, OP_DUP);

        /* Push pattern (strip quotes if present) */
        const char *pat = n->case_pats[i];
        vmc_compile_word(vs, pat);

        /* String compare: 0 = match */
        vm_buf_emit(vs->bc, OP_STRCMP);
        /* If non-zero (no match), skip to next pattern */
        int jnz_skip = vm_buf_emit_jump(vs->bc, OP_JNZ);

        /* Match: pop the duplicate value */
        vm_buf_emit(vs->bc, OP_POP);

        /* Compile the body */
        if(n->case_bodies[i]) vmc_compile_block(vs, n->case_bodies[i]);

        /* Jump to end of case */
        jmp_to_ends[n_ends++] = vm_buf_emit_jump(vs->bc, OP_JMP);

        /* Patch: no-match jumps here (next pattern) */
        vm_buf_patch(vs->bc, jnz_skip, vm_buf_pc(vs->bc));
    }

    /* Default case */
    if(n->case_default){
        /* Pop the case value */
        vm_buf_emit(vs->bc, OP_POP);
        if(n->case_default) vmc_compile_block(vs, n->case_default);
    } else {
        /* Pop the case value */
        vm_buf_emit(vs->bc, OP_POP);
    }

    /* Patch all JMP to end */
    int end_pc = vm_buf_pc(vs->bc);
    for(int i = 0; i < n_ends; i++){
        vm_buf_patch(vs->bc, jmp_to_ends[i], end_pc);
    }
}

/* Compile a pipe (NODE_PIPE) */
/* Build command string from a NODE_CMD */
static void vmc_build_cmdstr(char *buf, int bufsize, Node *n){
    int ci = 0;
    for(int i = 0; i < n->argc && ci < bufsize - 1; i++){
        if(i > 0) buf[ci++] = ' ';
        int al = strlen(n->argv[i]);
        if(ci + al < bufsize - 1){ memcpy(buf+ci, n->argv[i], al); ci += al; }
    }
    buf[ci] = 0;
}

/* Recursively build pipeline string for multi-stage pipes */
static void vmc_build_pipestr(char *buf, int bufsize, Node *n){
    if(!n) { buf[0] = 0; return; }
    if(n->type == NODE_CMD){
        vmc_build_cmdstr(buf, bufsize, n);
        return;
    }
    if(n->type == NODE_PIPE){
        char left[1024], right[1024];
        vmc_build_pipestr(left, sizeof(left), n->left);
        vmc_build_pipestr(right, sizeof(right), n->right);
        snprintf(buf, bufsize, "%s | %s", left, right);
        return;
    }
    buf[0] = 0;
}

static void vmc_compile_pipe(VmCompilerState *vs, Node *n){
    /* Build full pipeline string (handles multi-stage pipes) */
    char pipestr[4096];
    vmc_build_pipestr(pipestr, sizeof(pipestr), n);
    
    if(strlen(pipestr) > 0){
        /* Use popen to execute the full pipeline, print output to stdout */
        vmc_push_str(vs, pipestr);
        vm_buf_emit(vs->bc, OP_EXEC_CAP);
        vm_buf_emit(vs->bc, OP_PRINT); /* print captured output */
    }
}

/* Compile a single AST node */
static void vmc_compile_node(VmCompilerState *vs, Node *n){
    if(!n) return;

    switch(n->type){
        case NODE_CMD:
            vmc_compile_cmd(vs, n);
            break;
        case NODE_ASSIGN:
            vmc_compile_assign(vs, n);
            break;
        case NODE_IF:
            vmc_compile_if(vs, n);
            break;
        case NODE_WHILE:
            vmc_compile_while(vs, n);
            break;
        case NODE_FOR:
            vmc_compile_for(vs, n);
            break;
        case NODE_SELECT:
            /* SELECT not supported in VM mode — skip for now */
            break;
        case NODE_UNTIL:
            /* handled as while_negate */
            break;
        case NODE_ARITH:
            /* standalone ((expr)) — evaluate and discard */
            break;
        case NODE_FUNC:
            vmc_compile_func(vs, n);
            break;
        case NODE_PIPE:
            vmc_compile_pipe(vs, n);
            break;
        case NODE_CASE:
            vmc_compile_case(vs, n);
            break;
        case NODE_EXIT:
            if(n->exit_str){
                vmc_push_int(vs, atoi(n->exit_str));
            } else {
                vmc_push_int(vs, n->exit_code);
            }
            vm_buf_emit(vs->bc, OP_EXIT);
            break;
        case NODE_RETURN:
            if(n->exit_str){
                vmc_push_int(vs, atoi(n->exit_str));
            } else {
                vmc_push_int(vs, n->exit_code);
            }
            vm_buf_emit(vs->bc, OP_RET);
            break;
        case NODE_BREAK:
            /* JMP to loop break target — record patch location */
            if(vs->loop_depth > 0){
                int patch = vm_buf_emit_jump(vs->bc, OP_JMP);
                if(vs->break_count < 64){
                    vs->break_patches[vs->break_count++] = patch;
                }
            }
            break;
        case NODE_CONTINUE:
            /* JMP to end of current iteration */
            if(vs->loop_depth > 0){
                int patch = vm_buf_emit_jump(vs->bc, OP_JMP);
                if(vs->cont_count < 64){
                    vs->cont_patches[vs->cont_count++] = patch;
                }
            }
            break;
        case NODE_AND:
            /* Short-circuit AND — shell: 0=true, so JNZ (non-zero=false) skips right */
            if(n->left) vmc_compile_node(vs, n->left);
            int jz_and = vm_buf_emit_jump(vs->bc, OP_JNZ);
            if(n->right) vmc_compile_node(vs, n->right);
            vm_buf_patch(vs->bc, jz_and, vm_buf_pc(vs->bc));
            break;
        case NODE_OR:
            /* Short-circuit OR — shell: 0=true, so JZ (zero=true) skips right */
            if(n->left) vmc_compile_node(vs, n->left);
            int jnz_or = vm_buf_emit_jump(vs->bc, OP_JZ);
            if(n->right) vmc_compile_node(vs, n->right);
            vm_buf_patch(vs->bc, jnz_or, vm_buf_pc(vs->bc));
            break;
        case NODE_SUBSHELL:
        case NODE_GROUP:
            /* Just compile the inner block */
            if(n->left) vmc_compile_block(vs, n->left);
            break;
        case NODE_LOCAL:
        case NODE_EXPORT:
            /* Treat as assignment: VAR=value */
            if(n->argv){
                for(int i = 0; i < n->argc; i++){
                    char *eq = strchr(n->argv[i], '=');
                    if(eq){
                        *eq = 0;
                        vmc_push_str(vs, n->argv[i]);
                        vmc_compile_word(vs, eq + 1); /* expand $var */
                        vm_buf_emit(vs->bc, OP_SETENV);
                        vm_buf_emit(vs->bc, OP_POP);
                        *eq = '=';
                    }
                }
            }
            break;
        case NODE_UNSET:
            if(n->argv && n->argc > 0){
                for(int i = 0; i < n->argc; i++){
                    vmc_push_str(vs, n->argv[i]);
                    vmc_push_str(vs, "");
                    vm_buf_emit(vs->bc, OP_SETENV);
                }
            }
            break;
        default:
            /* Unsupported node — convert to shell command string and exec */
            {
                /* Build a shell representation of the node */
                char cmd[4096] = "";
                if(n->type == NODE_ARITH && n->rhs){
                    /* ((expr)) — evaluate as arithmetic */
                    snprintf(cmd, sizeof(cmd), ": $((%s))", n->rhs);
                } else if(n->type == NODE_HEREDOC && n->argv){
                    /* heredoc — skip for now */
                } else if(n->type == NODE_SOURCE && n->argv){
                    snprintf(cmd, sizeof(cmd), "source %s", n->argv[0]);
                } else if(n->type == NODE_EVAL && n->argv){
                    snprintf(cmd, sizeof(cmd), "eval %s", n->argv[0]);
                } else if(n->type == NODE_TRAP){
                    /* trap — skip */
                } else if(n->type == NODE_NOT && n->left){
                    /* ! command — skip for now */
                }
                if(cmd[0]){
                    vmc_push_str(vs, cmd);
                    vm_buf_emit(vs->bc, OP_EXEC_CMD);
                }
            }
            break;
    }
}

/* Compile a block (linked list of nodes) */
static void vmc_compile_block(VmCompilerState *vs, Node *n){
    while(n){
        vmc_compile_node(vs, n);
        n = n->next;
    }
}

/* Main entry: compile script AST to VM bytecode and emit output */
void vmc_compile_and_emit(FILE *out, Node *script, int obfuscate){
    VmBuf bc;
    VmConstPool cp;
    VmCompilerState vs;

    vm_buf_init(&bc);
    vm_cp_init(&cp);
    vs.bc = &bc;
    vs.cp = &cp;
    vs.loop_depth = 0;

    /* Pre-scan AST for function definitions */
    vm_func_count = 0;
    vm_prescan_funcs(script);

    /* Emit VM runtime */
    emit_vm_runtime(out);

    /* Compile AST */
    vmc_compile_block(&vs, script);

    /* Emit HALT */
    vm_buf_emit(&bc, OP_HALT);

    /* Emit output (const pool + bytecode + main) */
    vmc_emit_output(out, &bc, &cp, NULL, 0, obfuscate);

    /* Cleanup */
    vm_buf_free(&bc);
    vm_cp_free(&cp);
}


