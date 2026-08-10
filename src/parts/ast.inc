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

