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
    NODE_TRAP, NODE_SET, NODE_SELECT, NODE_UNTIL, NODE_ARITH
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

/* Full Node struct definition — shared between shell2c.c and VM compiler */
struct NodeExt;
typedef struct NodeExt {
    NodeType type;
    int lineno;
    struct NodeExt *next;
    /* assign */
    char *lhs, *rhs;
    /* cmd / background / subshell / group */
    char **argv; int argc;
    Redir *redirs;
    /* if */
    char *cond;
    struct NodeExt *then_blk, *else_blk;
    struct NodeExt *elif_conds[16]; struct NodeExt *elif_blks[16]; int elif_count;
    /* for */
    char *for_var; char **for_list; int for_len; struct NodeExt *body;
    int for_c_style;
    char *for_init, *for_cond, *for_update;
    char *for_init_raw, *for_cond_raw, *for_update_raw; /* pre-translation, for VM */
    /* while */
    char *while_cond; int while_negate; struct NodeExt *while_body;
    /* func */
    char *fname; struct NodeExt *func_body;
    /* exit/return */
    int exit_code; char *exit_str;
    /* pipe / and / or */
    struct NodeExt *left, *right;
    /* heredoc */
    char *heredoc_text;
    /* case */
    char *case_var;
    char *case_pats[128]; struct NodeExt *case_bodies[128]; int case_count;
    struct NodeExt *case_default;
    /* trap */
    char *trap_action; int trap_sig;
    /* set */
    char *set_opts;
} NodeExt;

#define Node NodeExt

/* new_redir defined in shell2c.c */
/* new_node defined in shell2c.c */

#endif /* S2C_AST_H */
