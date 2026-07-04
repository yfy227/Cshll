/*
 * s2c_emit.h — Code emitter declarations
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_EMIT_H
#define S2C_EMIT_H

#include "s2c_ast.h"
#include "s2c_symtab.h"

/* ExpandResult for string expansion */
#define EXPAND_MAX_ARGS 64
typedef struct {
    char *fmt;
    char *args[EXPAND_MAX_ARGS];
    int   nargs;
    int   arg_is_int[EXPAND_MAX_ARGS];
} ExpandResult;

void expand_string(const char *s, ExpandResult *er);
void expand_free(ExpandResult *er);

/* Expression translation */
char *translate_expr(const char *tok);
char *translate_brace_expansion(const char *body);
char *translate_num_operand(const char *tok);
char *translate_operand(const char *tok);

/* Condition translation */
char *translate_cond(const char *cond);
char *translate_test_unary(const char *op, const char *a1);
char *translate_test_binary(const char *op, const char *a1, const char *a2);

/* Word emission */
char *emit_word(FILE *out, const char *word);

/* Node emission */
/* emit_node defined in shell2c.c */
/* emit_functions defined in shell2c.c */
/* emit_command defined in shell2c.c */

/* Globals */
extern int tmp_id;
/* pending_pipe_cmd defined in shell2c.c */
extern int pipe_restore_needed;
extern int __redir_counter;

/* Redirect helpers */
int emit_redirs_save(FILE *out, Redir *r, int id);
void emit_redirs_apply(FILE *out, Redir *r, int id);
void emit_redirs_restore(FILE *out, Redir *r, int id);

/* Runtime library string */
extern const char *RT_HEADER;

#endif /* S2C_EMIT_H */
