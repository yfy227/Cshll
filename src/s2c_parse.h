/*
 * s2c_parse.h — Parser declarations
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_PARSE_H
#define S2C_PARSE_H

#include "s2c_ast.h"
#include "s2c_symtab.h"

/* Dynamic stack — no fixed limit, eliminates nesting depth truncation */
/* STACK_MAX removed; blk_stack is now a heap-allocated linked list */

typedef enum {
    BLK_IF_THEN, BLK_IF_ELIF, BLK_IF_ELSE,
    BLK_FOR, BLK_WHILE, BLK_FUNC, BLK_CASE, BLK_SUBSHELL, BLK_GROUP,
    BLK_SELECT, BLK_UNTIL
} BlkKind;

/* Forward declare NodeExt for BlkFrame */
struct NodeExt;
typedef struct BlkFrame {
    int kind;
    struct NodeExt *node;
    struct NodeExt **insert;
    struct NodeExt **parent_insert;
    struct BlkFrame *next;  /* linked list — replaces fixed array */
} BlkFrame;

/* blk_top is now the head pointer of the linked list (NULL = empty) */
/* blk_stack name removed — use blk_top directly */
extern BlkFrame *blk_top;      /* defined in parse.c */
extern Node *parse_root;       /* defined in parse.c */
extern Node **parse_insert;    /* defined in parse.c */
void parser_push(BlkFrame fr);
void parser_pop(void);

/* Tokenizer (defined in tokenizer.c) */
#define MAX_TOKS 2048
int tokenize(const char *line, char **toks, int maxtoks);
int expand_braces(char **toks, int ntoks, int maxtoks);

/* Parser (defined in parse.c) */
Node *parse_script(FILE *f);
void dispatch_segment(char **toks, int ntoks, int lineno);

/* Helpers (defined in parse.c) */
int is_assignment(const char *tok);
int is_array_assignment(const char *tok);
void extract_array_assign(const char *t, char *name, int name_sz,
                          char *key, int key_sz, char *val, int val_sz,
                          int *is_append);
void strip_comment(char *line);

#endif /* S2C_PARSE_H */
