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
/* blk_top, parse_root, parse_insert defined in shell2c.c */

/* Tokenizer */
#define MAX_TOKS 2048
/* tokenize defined in shell2c.c */
/* expand_braces defined in shell2c.c */

/* Parser */
/* parse_script defined in shell2c.c */
/* dispatch_segment defined in shell2c.c */
/* make_cmd defined in shell2c.c */

/* Helpers */
/* is_assignment defined in shell2c.c */
/* is_array_assignment defined in shell2c.c */
/* extract_array_assign defined in shell2c.c */
/* find_op defined in shell2c.c */
/* strip_comment defined in shell2c.c */

#endif /* S2C_PARSE_H */
