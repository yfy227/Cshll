/* ================================================================== */
/* L3  AST — types in s2c_ast.h, constructors here                     */
/* ================================================================== */

/* NodeExt is now defined in s2c_ast.h */

Node *new_node(NodeType t, int ln){
    Node *n=calloc(1,sizeof(Node));
    n->type=t; n->lineno=ln;
    return n;
}

Redir *new_redir(int fd,const char *file,int append,
                        int dup_fd,int is_hd,int is_hs,const char *hdtext){
    Redir *r=calloc(1,sizeof(Redir));
    r->fd=fd;
    r->file=file?xstrdup(file):NULL;
    r->append=append;
    r->dup_fd=dup_fd;
    r->is_heredoc=is_hd;
    r->is_herestr=is_hs;
    r->heredoc=hdtext?xstrdup(hdtext):NULL;
    return r;
}

static void free_redirs(Redir *r){
    while(r){
        Redir *nx=r->next;
        free(r->file); free(r->heredoc); free(r);
        r=nx;
    }
}

/* Recursively free an AST subtree. Call after emission is complete —
 * node strings are owned by the tree (xstrdup'd at construction).
 * Establishes clear ownership: parser allocates, emitter borrows,
 * transpile_thread frees the root when done. */
void free_node(Node *n){
    if(!n) return;
    /* free children first */
    free_node(n->then_blk);
    free_node(n->else_blk);
    for(int i=0;i<n->elif_count && i<16;i++){
        free_node(n->elif_conds[i]);
        free_node(n->elif_blks[i]);
    }
    free_node(n->body);
    free_node(n->while_body);
    free_node(n->func_body);
    free_node(n->left);
    free_node(n->right);
    for(int i=0;i<n->case_count && i<128;i++){
        free(n->case_pats[i]);
        free_node(n->case_bodies[i]);
    }
    free_node(n->case_default);
    /* free owned strings */
    free(n->lhs); free(n->rhs); free(n->cond);
    free(n->for_var); free(n->for_init); free(n->for_cond); free(n->for_update);
    free(n->for_init_raw); free(n->for_cond_raw); free(n->for_update_raw);
    free(n->while_cond); free(n->fname); free(n->exit_str);
    free(n->heredoc_text); free(n->case_var); free(n->case_var_raw);
    free(n->trap_action); free(n->set_opts);
    /* free arrays */
    for(int i=0;i<n->argc;i++) free(n->argv[i]);
    free(n->argv);
    for(int i=0;i<n->for_len;i++) free(n->for_list[i]);
    free(n->for_list);
    free_redirs(n->redirs);
    /* free sibling chain and the node itself */
    Node *nx=n->next;
    free(n);
    free_node(nx);
}

