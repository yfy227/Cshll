/*
 * s2c_parse.h — Parser declarations
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_PARSE_H
#define S2C_PARSE_H

#include "s2c_ast.h"
#include "s2c_symtab.h"

#define STACK_MAX 64

typedef enum {
    BLK_IF_THEN, BLK_IF_ELIF, BLK_IF_ELSE,
    BLK_FOR, BLK_WHILE, BLK_FUNC, BLK_CASE, BLK_SUBSHELL, BLK_GROUP
} BlkKind;

/* Forward declare NodeExt for BlkFrame */
struct NodeExt;
typedef struct {
    int kind;
    struct NodeExt *node;
    struct NodeExt **insert;
    struct NodeExt **parent_insert;
} BlkFrame;

/* blk_stack defined in shell2c.c */
/* blk_top defined in shell2c.c */
/* parse_root defined in shell2c.c */
/* parse_insert defined in shell2c.c */

/* Tokenizer */
#define MAX_TOKS 512
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
