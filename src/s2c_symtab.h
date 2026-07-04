/*
 * s2c_symtab.h — Symbol table and function registry
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_SYMTAB_H
#define S2C_SYMTAB_H

#include "s2c_common.h"

typedef enum { V_INT, V_STR, V_ARRAY, V_UNKNOWN } VarKind;
typedef struct { char name[128]; VarKind kind; int is_local; } VarInfo;

#define MAX_VARS 4096
extern VarInfo var_table[MAX_VARS];
extern int var_count;

void add_var(const char *name, VarKind k);
VarKind get_var_kind(const char *name);
int is_known_var(const char *name);
const char *var_c_expr(const char *name);

/* Function table */
#define MAX_FUNCS 512
extern char *func_table[MAX_FUNCS];
extern int func_count;

void register_func(const char *name);
int is_user_func(const char *name);

/* Heredoc table */
#define MAX_HEREDOCS 64
typedef struct { char *text; int expand; } HeredocEntry;
extern HeredocEntry heredoc_table[MAX_HEREDOCS];
extern int heredoc_count;
extern int heredoc_next;

int heredoc_store(const char *text, int expand);
const char *heredoc_consume(int *expand);

#endif /* S2C_SYMTAB_H */
