/*
 * s2c_ast.h — AST node types and structures
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_AST_H
#define S2C_AST_H

#include "s2c_common.h"

typedef enum {
    NODE_CMD, NODE_ASSIGN, NODE_IF, NODE_FOR, NODE_WHILE,
    NODE_PIPE, NODE_HEREDOC, NODE_BACKGROUND, NODE_CASE,
    NODE_FUNC, NODE_RETURN, NODE_BREAK, NODE_CONTINUE, NODE_EXIT,
    NODE_AND, NODE_OR, NODE_NOT, NODE_SUBSHELL, NODE_GROUP,
    NODE_LOCAL, NODE_EXPORT, NODE_UNSET, NODE_SOURCE, NODE_EVAL,
    NODE_TRAP, NODE_SET
} NodeType;

typedef struct Redir {
    int fd;
    char *file;
    char *heredoc;
    int append;
    int dup_fd;
    int is_heredoc;
    int is_herestr;
    int hd_expand;
    int fd_high;
    struct Redir *next;
} Redir;

/* Node struct defined in shell2c.c as NodeExt */

/* new_redir defined in shell2c.c */
/* new_node defined in shell2c.c */

#endif /* S2C_AST_H */
