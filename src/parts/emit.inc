/* ================================================================== */
/* L8  Code emitter                                                   */
/* ================================================================== */

int tmp_id=0;
void emit_node(FILE *out, Node *n);
static void emit_block(FILE *out, Node *n);
Node *pending_pipe_cmd=NULL;
int pipe_restore_needed=0;

/* Emit a word as a C string expression. Returns heap string naming a C lvalue/expression. */
char *emit_word(FILE *out, const char *word){
    if(!word) return xstrdup("\"\"");
    /* process substitution: <(cmd) or >(cmd) */
    if((word[0]=='<'||word[0]=='>')&&word[1]=='('){
        char dir=word[0]; /* '<' for input, '>' for output */
        char cmd[2048]; int ci=0;
        const char *p=word+2; int d=1;
        while(*p && d && ci<(int)sizeof(cmd)-1){
            if(*p=='(') d++;
            else if(*p==')'){ d--; if(d==0) break; }
            cmd[ci++]=*p++;
        }
        cmd[ci]=0;
        /* Check if cmd is a bare user function name */
        int is_bare_func = 1;
        for(const char *c=cmd;*c;c++){
            if(!isalnum((unsigned char)*c)&&*c!='_'){ is_bare_func=0; break; }
        }
        if(is_bare_func && is_user_func(cmd)){
            /* User function: capture output to a temp file, return its path */
            int id=tmp_id++;
            fprintf(out,"    /* proc_subst for user func %s */\n",cmd);
            fprintf(out,"    char __ps_%d[128]; { const char *__out=__sh_capture_fn((void(*)(int,char**))%s,0,NULL); FILE *__tf=tmpfile(); if(__tf){ fputs(__out,__tf); fflush(__tf); int __tfd=fileno(__tf); snprintf(__ps_%d,sizeof(__ps_%d),\"/dev/fd/%%d\",__tfd); } else { __ps_%d[0]=0; } }\n",id,cmd,id,id,id);
            char *r=malloc(32); snprintf(r,32,"__ps_%d",id);
            return r;
        }
        /* escape for C string */
        char esc[2100]; int ei=0;
        for(int i=0;cmd[i]&&ei<(int)sizeof(esc)-4;i++){
            if(cmd[i]=='"'||cmd[i]=='\\') esc[ei++]='\\';
            esc[ei++]=cmd[i];
        }
        esc[ei]=0;
        int id=tmp_id++;
        fprintf(out,"    char __ps_%d[128]; snprintf(__ps_%d,sizeof(__ps_%d),\"%%s\",__sh_proc_subst(\"%s\",'%c'));\n",id,id,id,esc,dir);
        char *r=malloc(32); snprintf(r,32,"__ps_%d",id);
        return r;
    }

    /* Unescape the word: handle mixed quoting (e.g. 'It'\''s' = It's).
     * Walk through the token, handling each segment:
     * - '...' → literal (no expansion)
     * - "..." → with variable expansion and escape processing
     * - \X → literal X (outside quotes)
     * - bare chars → literal (or variable expansion if $) */
    char unescaped[4096]; int ui = 0;
    int has_dollar = 0;  /* flag: does the unescaped result contain $ for expansion? */
    const char *p = word;

    while(*p && ui < (int)sizeof(unescaped)-1){
        if(*p == '\''){
            /* single-quoted segment: literal, no expansion */
            p++;
            while(*p && *p != '\'' && ui < (int)sizeof(unescaped)-1){
                unescaped[ui++] = *p++;
            }
            if(*p == '\'') p++;
        } else if(*p == '"'){
            /* double-quoted segment: variable expansion applies, backslash before $ ` " \ newline */
            p++;
            while(*p && *p != '"' && ui < (int)sizeof(unescaped)-1){
                if(*p == '\\' && *(p+1)){
                    char next = *(p+1);
                    if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n'){
                        unescaped[ui++] = next;
                        p += 2;
                    } else {
                        unescaped[ui++] = *p++;
                    }
                } else if(*p == '$' && *(p+1)=='('){
                    /* $(...) inside double quotes — copy intact, skip inner quotes */
                    unescaped[ui++] = *p++;
                    unescaped[ui++] = *p++;
                    int d=1;
                    while(*p && d && ui<(int)sizeof(unescaped)-1){
                        if(*p=='"'){ unescaped[ui++]=*p++; while(*p&&*p!='"'&&ui<(int)sizeof(unescaped)-1) unescaped[ui++]=*p++; if(*p=='"') unescaped[ui++]=*p++; continue; }
                        if(*p=='\''){ unescaped[ui++]=*p++; while(*p&&*p!='\''&&ui<(int)sizeof(unescaped)-1) unescaped[ui++]=*p++; if(*p=='\'') unescaped[ui++]=*p++; continue; }
                        if(*p=='(')d++; else if(*p==')')d--;
                        unescaped[ui++]=*p++;
                    }
                    has_dollar = 1;
                } else if(*p == '$'){
                    has_dollar = 1;
                    unescaped[ui++] = *p++;
                } else {
                    unescaped[ui++] = *p++;
                }
            }
            if(*p == '"') p++;
        } else if(*p == '\\' && *(p+1)){
            /* escaped character outside quotes → literal */
            unescaped[ui++] = *(p+1);
            p += 2;
        } else if(*p == '$'){
            has_dollar = 1;
            unescaped[ui++] = *p++;
        } else {
            unescaped[ui++] = *p++;
        }
    }
    unescaped[ui] = 0;

    /* Now emit the unescaped string */
    if(!has_dollar && !strchr(unescaped,'\\')){
        /* Pure literal — escape for C string and return */
        char *esc = c_escape_literal(unescaped);
        char *r = malloc(strlen(esc) + 3);
        sprintf(r, "\"%s\"", esc);
        free(esc);
        return r;
    }
    if(!has_dollar){
        /* Has backslashes but no $ — still a literal, just escape for C */
        char *esc = c_escape_literal(unescaped);
        char *r = malloc(strlen(esc) + 3);
        sprintf(r, "\"%s\"", esc);
        free(esc);
        return r;
    }
    /* Has $ — use expand_string for variable expansion */
    ExpandResult er; expand_string(unescaped,&er);
    int id=tmp_id++;
    /* Escape newlines/tabs in fmt for C string literal */
    fprintf(out,"    char __tw_%d[1024]; snprintf(__tw_%d, sizeof(__tw_%d), \"",id,id,id);
    for(const char *c=er.fmt;*c;c++){
        if(*c=='\n') fprintf(out,"\\n");
        else if(*c=='\t') fprintf(out,"\\t");
        else if(*c=='\r') fprintf(out,"\\r");
        else if(*c=='"') fprintf(out,"\\\"");
        else if(*c=='\\') fprintf(out,"\\\\");
        else fprintf(out,"%c",*c);
    }
    fprintf(out,"\"");
    for(int i=0;i<er.nargs;i++) fprintf(out,", %s",er.args[i]);
    fprintf(out,");\n");
    expand_free(&er);
    char *r=malloc(32); snprintf(r,32,"__tw_%d",id);
    return r;
}

/* Emit redirects: returns number of saved-fd statements emitted.
 * Each saved fd is named __sfd_<id>_<fd>. Caller must emit restore after. */
int __redir_counter = 0;

int emit_redirs_save(FILE *out, Redir *r, int id){
    (void)id;
    int myid = __redir_counter++;
    int count=0;
    for(Redir *p=r;p;p=p->next){
        fprintf(out,"    int __sfd_%d_%d=dup(%d);\n",myid,p->fd,p->fd);
        count++;
    }
    /* stash myid in the first redir's fd_high for restore to find */
    if(r) r->fd_high = myid;
    return count;
}

void emit_redirs_apply(FILE *out, Redir *r, int id){
    (void)id;
    for(Redir *p=r;p;p=p->next){
        if(p->is_herestr){
            /* here-string: evaluate the word, write to temp file, dup as fd */
            char *w=emit_word(out,p->heredoc?p->heredoc:"\"\"");
            fprintf(out,"    { FILE *__hf=tmpfile(); if(__hf){ fputs(%s,__hf); fputc('\\n',__hf); fflush(__hf); int __hfd=fileno(__hf); lseek(__hfd,0,SEEK_SET); dup2(__hfd,%d); clearerr(stdin); } }\n",w,p->fd);
            free(w);
        } else if(p->is_heredoc){
            /* heredoc: write content to temp file.
             * fd_high stores expand flag (1=expand vars, 0=literal). */
            const char *content = p->heredoc?p->heredoc:"";
            int do_expand = p->hd_expand;
            if(do_expand && strchr(content,'$')){
                /* expand variables using expand_string */
                ExpandResult er; expand_string(content,&er);
                fprintf(out,"    { FILE *__hf=tmpfile(); if(__hf){ char __hdb[16384]; snprintf(__hdb,sizeof(__hdb),\"");
                /* escape the format string */
                for(const char *c=er.fmt;*c;c++){
                    if(*c=='\\'||*c=='"') fprintf(out,"\\");
                    if(*c=='\n') fprintf(out,"\\n");
                    else if(*c=='\t') fprintf(out,"\\t");
                    else fprintf(out,"%c",*c);
                }
                fprintf(out,"\"");
                for(int ai=0;ai<er.nargs;ai++){
                    fprintf(out,",%s",er.args[ai]);
                }
                fprintf(out,"); fputs(__hdb,__hf); fflush(__hf); int __hfd=fileno(__hf); lseek(__hfd,0,SEEK_SET); dup2(__hfd,%d); clearerr(stdin); } }\n",p->fd);
                expand_free(&er);
            } else {
                /* literal content (quoted delimiter or no vars) */
                fprintf(out,"    { FILE *__hf=tmpfile(); if(__hf){ fputs(\"");
                for(const char *c=content;*c;c++){
                    if(*c=='\\'||*c=='"') fprintf(out,"\\");
                    if(*c=='\n') fprintf(out,"\\n");
                    else if(*c=='\t') fprintf(out,"\\t");
                    else fprintf(out,"%c",*c);
                }
                fprintf(out,"\",__hf); fflush(__hf); int __hfd=fileno(__hf); lseek(__hfd,0,SEEK_SET); dup2(__hfd,%d); clearerr(stdin); } }\n",p->fd);
            }
        } else if(p->dup_fd>=0){
            fprintf(out,"    dup2(%d,%d);\n",p->dup_fd,p->fd);
        } else {
            char *w=emit_word(out,p->file);
            if(p->fd==0){
                fprintf(out,"    { int __fd=open(%s,O_RDONLY); if(__fd>=0){dup2(__fd,0);close(__fd);clearerr(stdin);} else perror(\"<\"); }\n",w);
            } else {
                fprintf(out,"    { int __fd=open(%s,%s,0644); if(__fd>=0){dup2(__fd,%d);close(__fd);} else perror(\">\"); }\n",
                        w, p->append?"O_WRONLY|O_CREAT|O_APPEND":"O_WRONLY|O_CREAT|O_TRUNC", p->fd);
            }
            free(w);
        }
    }
}

void emit_redirs_restore(FILE *out, Redir *r, int id){
    (void)id;
    int myid = r ? r->fd_high : 0;
    /* restore in reverse order */
    Redir *stack[32]; int sp=0;
    for(Redir *p=r;p&&sp<32;p=p->next) stack[sp++]=p;
    for(int i=sp-1;i>=0;i--){
        fprintf(out,"    fflush(%s); dup2(__sfd_%d_%d,%d); close(__sfd_%d_%d);\n",
                stack[i]->fd==1?"stdout":(stack[i]->fd==2?"stderr":"NULL"),
                myid,stack[i]->fd,stack[i]->fd, myid,stack[i]->fd);
    }
}

static void emit_echo(FILE *out, char **argv, int argc){
    int no_nl=0,esc=0,start=1;
    while(start<argc && argv[start][0]=='-'){
        int ok=1;
        for(int k=1;argv[start][k];k++){
            if(argv[start][k]=='n') no_nl=1;
            else if(argv[start][k]=='e') esc=1;
            else if(argv[start][k]=='E') esc=0;
            else { ok=0; break; }
        }
        if(ok) start++; else break;
    }
    int nargs=argc-start;
    /* Case 1: single literal, no escape, with newline → __sh_puts */
    if(!esc && !no_nl && nargs==1){
        char *w=emit_word(out,argv[start]);
        if(w[0]=='"'){
            fprintf(out,"    __sh_puts(%s);\n",w);
        } else if(strncmp(w,"__tw_",5)==0){
            /* temp buffer from variable expansion — use __sh_putf */
            fprintf(out,"    __sh_putf(\"%%s\", %s);\n",w);
        } else {
            fprintf(out,"    __sh_putf(\"%%s\", %s);\n",w);
        }
        free(w);
        return;
    }
    /* Case 2: single -n argument (no newline) */
    if(!esc && no_nl && nargs==1){
        char *w=emit_word(out,argv[start]);
        fprintf(out,"    fputs(%s,stdout);\n",w);
        free(w);
        return;
    }
    /* General case: multiple arguments or escape mode */
    for(int i=start;i<argc;i++){
        if(i>start) fprintf(out,"    putchar(' ');\n");
        char *w=emit_word(out,argv[i]);
        if(esc)
            fprintf(out,"    __sh_echo_escape(%s);\n",w);
        else
            fprintf(out,"    fputs(%s,stdout);\n",w);
        free(w);
    }
    if(!no_nl) fprintf(out,"    putchar('\\n');\n");
}

static void emit_pipe(FILE *out, Node *n){
    /* collect pipeline stages into a list */
    Node *stages[16]; int ns=0;
    /* n is NODE_PIPE with left/right; right may itself be NODE_PIPE */
    /* walk: for a PIPE node, left is a stage, right is the rest */
    Node *p=n;
    while(p && p->type==NODE_PIPE && ns<15){
        stages[ns++]=p->left;
        p=p->right;
    }
    if(p) stages[ns++]=p;

    int id=n->lineno;
    fprintf(out,"    { /* pipeline of %d stages */\n",ns);
    /* save original stdin */
    fprintf(out,"    int __psi%d=dup(STDIN_FILENO);\n",id);
    fprintf(out,"    int __pso%d=dup(STDOUT_FILENO);\n",id);
    /* chain: for each stage except last, fork, in child set stdout=pipe write, emit, exit;
     * in parent set stdin=pipe read, continue. Last stage runs in parent. */
    for(int i=0;i<ns-1;i++){
        fprintf(out,"    int __pfd%d_%d[2]; if(pipe(__pfd%d_%d)<0){perror(\"pipe\");exit(1);}\n",id,i,id,i);
        fprintf(out,"    pid_t __ppid%d_%d=fork();\n",id,i);
        fprintf(out,"    if(__ppid%d_%d==0){\n",id,i);
        fprintf(out,"        close(__pfd%d_%d[0]); dup2(__pfd%d_%d[1],STDOUT_FILENO); close(__pfd%d_%d[1]);\n",id,i,id,i,id,i);
        emit_node(out,stages[i]);
        fprintf(out,"        fflush(stdout); _exit(0);\n");
        fprintf(out,"    } else {\n");
        fprintf(out,"        close(__pfd%d_%d[1]); dup2(__pfd%d_%d[0],STDIN_FILENO); close(__pfd%d_%d[0]); clearerr(stdin);\n",id,i,id,i,id,i);
        fprintf(out,"        waitpid(__ppid%d_%d,NULL,0);\n",id,i);
        fprintf(out,"    }\n");
    }
    /* last stage in parent */
    emit_node(out,stages[ns-1]);
    /* restore stdin/stdout */
    fprintf(out,"    fflush(stdout); dup2(__pso%d,STDOUT_FILENO); close(__pso%d);\n",id,id);
    fprintf(out,"    dup2(__psi%d,STDIN_FILENO); close(__psi%d); clearerr(stdin);\n",id,id);
    fprintf(out,"    }\n");
}

/* Dispatch a single command word to its builtin or function call. */
void emit_command(FILE *out, char **argv, int ac, int id){
    const char *cmd=argv[0];
    /* internal: arithmetic command from (( expr )) */
    if(!strcmp(cmd,"__arith")&&ac>=2){
        fprintf(out,"    __exit_status=((%s)!=0);\n",argv[1]);
        return;
    }
    if(!strcmp(cmd,"echo")){ emit_echo(out,argv,ac); return; }
    if(!strcmp(cmd,"printf")){
        /* BUG-16 fix: printf -v var format args → __sh_sprintf into var */
        if(ac>=3 && !strcmp(argv[1],"-v")){
            const char *vname=argv[2];
            if(!is_known_var(vname)) add_var(vname,V_STR);
            const char *fmt=argv[3];
            int flen=(int)strlen(fmt);
            int is_literal=(flen>=2 && fmt[0]=='"' && fmt[flen-1]=='"');
            if(is_literal && !strchr(fmt,'$')){
                fprintf(out,"    __sh_sprintf(%s,sizeof(%s),%s",safe_cname(vname),safe_cname(vname),fmt);
            } else {
                char *w=emit_word(out,argv[3]);
                fprintf(out,"    { const char *__pf=%s; __sh_sprintf(%s,sizeof(%s),__pf",w,safe_cname(vname),safe_cname(vname));
                free(w);
            }
            for(int i=4;i<ac;i++){ char *a=emit_word(out,argv[i]); fprintf(out,",%s",a); free(a); }
            if(is_literal && !strchr(fmt,'$')){
                fprintf(out,");\n");
            } else {
                fprintf(out,"); }\n");
            }
            return;
        }
        if(ac>=2){
            /* printf format string: don't escape % like echo does */
            const char *fmt = argv[1];
            int flen = (int)strlen(fmt);
            int is_literal = (flen>=2 && fmt[0]=='"' && fmt[flen-1]=='"');
            /* Pre-emit non-literal args to temp buffers first */
            char *argstrs[64]; int nargstrs=0;
            for(int i=2;i<ac && nargstrs<63;i++){
                argstrs[nargstrs++]=emit_word(out,argv[i]);
            }
            if(is_literal && !strchr(fmt,'$')){
                /* Literal format string — use directly */
                fprintf(out,"    __sh_printf(%s", fmt);
            } else {
                /* Has variables — use emit_word but fix %% back to % */
                char *w=emit_word(out,argv[1]);
                fprintf(out,"    { const char *__pf=%s; __sh_printf(__pf",w);
                free(w);
            }
            for(int i=0;i<nargstrs;i++){ fprintf(out,",%s",argstrs[i]); free(argstrs[i]); }
            if(is_literal && !strchr(fmt,'$')){
                fprintf(out,");\n");
            } else {
                fprintf(out,"); }\n");
            }
        }
        return;
    }
    if(!strcmp(cmd,"cd")){
        if(ac>1){ char *w=emit_word(out,argv[1]);
            fprintf(out,"    if(chdir(%s)!=0){perror(\"cd\");__exit_status=1;}else __exit_status=0;\n",w); free(w); }
        else fprintf(out,"    { const char*__h=getenv(\"HOME\");if(__h){if(chdir(__h)!=0)perror(\"cd\");} }\n");
        return;
    }
    if(!strcmp(cmd,"pwd")){ fprintf(out,"    __b_pwd();\n"); return; }
    if(!strcmp(cmd,"ls")){
        /* collect flags and paths */
        char flags[64]=""; int fi=0;
        const char *paths[32]; int np=0;
        for(int i=1;i<ac;i++){
            if(argv[i][0]=='-'&&argv[i][1]){
                for(int k=1;argv[i][k]&&fi<62;k++) flags[fi++]=argv[i][k];
            } else if(np<31) paths[np++]=argv[i];
        }
        flags[fi]=0;
        if(np==0) paths[np++]=".";
        fprintf(out,"    __b_ls(\"%s\"",flags[0]?flags:"");
        for(int i=0;i<np;i++){ char *w=emit_word(out,paths[i]); fprintf(out,",%s",w); free(w); }
        fprintf(out,",NULL);\n");
        return;
    }
    if(!strcmp(cmd,"cat")){
        int nflags=0; const char *files[32]; int nf=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-n")) nflags=1;
            else if(!strcmp(argv[i],"-E")) nflags|=2;
            else if(nf<31) files[nf++]=argv[i];
        }
        if(nf==0){ fprintf(out,"    __b_cat(NULL,%d);\n",nflags); }
        else for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_cat(%s,%d);\n",w,nflags); free(w); }
        return;
    }
    if(!strcmp(cmd,"grep")){
        char flags[32]=""; int fi=0;
        const char *pat=NULL; const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++){
            if(argv[i][0]=='-'&&argv[i][1]&&argv[i][1]!=' '){
                for(int k=1;argv[i][k]&&fi<30;k++) flags[fi++]=argv[i][k];
            } else if(!pat) pat=argv[i];
            else if(nf<15) files[nf++]=argv[i];
        }
        flags[fi]=0;
        char *pw=pat?emit_word(out,pat):xstrdup("\"\"");
        if(nf==0) fprintf(out,"    __b_grep(%s,NULL,\"%s\");\n",pw,flags);
        else { for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_grep(%s,%s,\"%s\");\n",pw,w,flags); free(w); } }
        free(pw);
        return;
    }
    if(!strcmp(cmd,"sed")){
        /* only s/old/new/[g] form supported */
        const char *script=NULL; const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++){
            if(argv[i][0]=='-'&&argv[i][1]=='e'&&i+1<ac) script=argv[++i];
            else if(!script && argv[i][0]=='s'){ script=argv[i]; }
            else if(nf<15) files[nf++]=argv[i];
        }
        char *sw=script?emit_word(out,script):xstrdup("\"\"");
        if(nf==0) fprintf(out,"    __b_sed(%s,NULL);\n",sw);
        else { for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_sed(%s,%s);\n",sw,w); free(w); } }
        free(sw);
        return;
    }
    if(!strcmp(cmd,"tr")){
        if(ac>=2 && !strcmp(argv[1],"-d")){
            /* tr -d SET: delete characters in SET */
            if(ac>=3){
                char *a=emit_word(out,argv[2]);
                fprintf(out,"    __b_tr_delete(%s);\n",a); free(a);
            }
        } else if(ac>=3){
            char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_tr(%s,%s);\n",a,b); free(a); free(b);
        }
        return;
    }
    if(!strcmp(cmd,"cut")){
        char delim=' '; const char *fields=NULL; const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-d")&&i+1<ac){
                const char *dv=argv[++i];
                /* handle single-quoted delimiter: 'x' → x */
                if(dv[0]=='\'' && dv[1] && dv[2]=='\'') delim=dv[1];
                else if(dv[0]=='\\') delim=dv[1]; /* escaped char */
                else if(dv[0]=='"' && dv[strlen(dv)-1]=='"'){
                    /* double-quoted delimiter */
                    char tmp[32]; strncpy(tmp,dv+1,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
                    int tl=(int)strlen(tmp); if(tl>0&&tmp[tl-1]=='"') tmp[--tl]=0;
                    delim=tmp[0];
                }
                else delim=dv[0];
            }
            else if(!strcmp(argv[i],"-f")&&i+1<ac) fields=argv[++i];
            else if(!strncmp(argv[i],"-f",2)) fields=argv[i]+2; /* -f2 style */
            else if(!strncmp(argv[i],"-d",2)){
                /* -d' ' combined option */
                const char *dv=argv[i]+2;
                if(dv[0]=='\'' && dv[1] && dv[2]=='\'') delim=dv[1];
                else if(dv[0]=='\\') delim=dv[1];
                else delim=dv[0];
            }
            else if(nf<15) files[nf++]=argv[i];
        }
        char *fw=fields?emit_word(out,fields):xstrdup("\"\"");
        /* Handle special characters in delim that break C char literal */
        if(delim=='\'') fprintf(out,"    __b_cut(%s,'\\'',NULL);\n",fw);
        else if(delim=='\\') fprintf(out,"    __b_cut(%s,'\\\\',NULL);\n",fw);
        else if(delim==0) fprintf(out,"    __b_cut(%s,' ',NULL);\n",fw);
        else {
            if(nf==0) fprintf(out,"    __b_cut(%s,'%c',NULL);\n",fw,delim);
            else for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_cut(%s,'%c',%s);\n",fw,delim,w); free(w); }
        }
        free(fw);
        return;
    }
    if(!strcmp(cmd,"sort")){
        int rev=0,uniq=0,num=0; const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-r")) rev=1;
            else if(!strcmp(argv[i],"-u")) uniq=1;
            else if(!strcmp(argv[i],"-n")) num=1;
            else if(nf<15) files[nf++]=argv[i];
        }
        if(nf==0) fprintf(out,"    __b_sort(NULL,%d,%d,%d);\n",rev,uniq,num);
        else for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_sort(%s,%d,%d,%d);\n",w,rev,uniq,num); free(w); }
        return;
    }
    if(!strcmp(cmd,"uniq")){
        int count=0; const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-c")) count=1;
            else if(nf<15) files[nf++]=argv[i];
        }
        if(nf==0) fprintf(out,"    __b_uniq(NULL,%d);\n",count);
        else for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_uniq(%s,%d);\n",w,count); free(w); }
        return;
    }
    if(!strcmp(cmd,"wc")){
        int wl=0,ww=0,wc_c=0; const char *file=NULL;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-l"))wl=1;
            else if(!strcmp(argv[i],"-w"))ww=1;
            else if(!strcmp(argv[i],"-c"))wc_c=1;
            else if(!strcmp(argv[i],"-m"))wc_c=1;
            else file=argv[i];
        }
        if(!wl&&!ww&&!wc_c){wl=ww=wc_c=1;}
        char *fw=file?emit_word(out,file):xstrdup("NULL");
        fprintf(out,"    __b_wc(%s,%d,%d,%d);\n",fw,wl,ww,wc_c); free(fw);
        return;
    }
    if(!strcmp(cmd,"head")||!strcmp(cmd,"tail")){
        int nl=10; const char *file=NULL;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-n")&&i+1<ac) nl=atoi(argv[++i]);
            else if(argv[i][0]=='-'&&isdigit((unsigned char)argv[i][1])) nl=atoi(argv[i]+1);
            else file=argv[i];
        }
        char *fw=file?emit_word(out,file):xstrdup("NULL");
        if(!strcmp(cmd,"head")) fprintf(out,"    __b_head(%s,%d);\n",fw,nl);
        else fprintf(out,"    __b_tail(%s,%d);\n",fw,nl);
        free(fw);
        return;
    }
    if(!strcmp(cmd,"cp")){
        if(ac>2){char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_cp(%s,%s);\n",a,b);free(a);free(b);}
        return;
    }
    if(!strcmp(cmd,"mv")){
        if(ac>2){char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_mv(%s,%s);\n",a,b);free(a);free(b);}
        return;
    }
    if(!strcmp(cmd,"rm")){
        int rf=0; const char *files[32]; int nf=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-r")||!strcmp(argv[i],"-rf")||!strcmp(argv[i],"-fr")) rf=1;
            else if(!strcmp(argv[i],"-f")) ;
            else if(nf<31) files[nf++]=argv[i];
        }
        for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"    __b_rm(%s,%d);\n",w,rf); free(w); }
        return;
    }
    if(!strcmp(cmd,"mkdir")){
        int p=0; const char *dirs[32]; int nd=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-p")) p=1;
            else if(nd<31) dirs[nd++]=argv[i];
        }
        for(int i=0;i<nd;i++){ char *w=emit_word(out,dirs[i]); fprintf(out,"    __b_mkdir(%s,%d);\n",w,p); free(w); }
        return;
    }
    if(!strcmp(cmd,"rmdir")){
        for(int i=1;i<ac;i++){char *w=emit_word(out,argv[i]);
            fprintf(out,"    __b_rmdir(%s);\n",w);free(w);}
        return;
    }
    if(!strcmp(cmd,"touch")){
        for(int i=1;i<ac;i++){char *w=emit_word(out,argv[i]);
            fprintf(out,"    __b_touch(%s);\n",w);free(w);}
        return;
    }
    if(!strcmp(cmd,"ln")){
        int sym=0; const char *a=NULL,*b=NULL;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-s")) sym=1;
            else if(!a) a=argv[i];
            else if(!b) b=argv[i];
        }
        if(a&&b){ char *wa=emit_word(out,a),*wb=emit_word(out,b);
            fprintf(out,"    __b_ln(%s,%s,%d);\n",wa,wb,sym); free(wa); free(wb); }
        return;
    }
    if(!strcmp(cmd,"chmod")){
        if(ac>=3){ char *m=emit_word(out,argv[1]);
            for(int i=2;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_chmod(%s,%s);\n",m,w); free(w); }
            free(m); }
        return;
    }
    if(!strcmp(cmd,"chown")){
        if(ac>=3){ char *o=emit_word(out,argv[1]);
            for(int i=2;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_chown(%s,%s);\n",o,w); free(w); }
            free(o); }
        return;
    }
    if(!strcmp(cmd,"stat")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_stat(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"du")){
        for(int i=1;i<ac;i++){ if(argv[i][0]=='-') continue; char *w=emit_word(out,argv[i]); fprintf(out,"    __b_du(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_du(\".\");\n");
        return;
    }
    if(!strcmp(cmd,"df")){
        fprintf(out,"    __b_df();\n"); return;
    }
    if(!strcmp(cmd,"file")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_file(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"basename")){
        if(ac>1){ char *w=emit_word(out,argv[1]); fprintf(out,"    __b_basename(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"dirname")){
        if(ac>1){ char *w=emit_word(out,argv[1]); fprintf(out,"    __b_dirname(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"realpath")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_realpath(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"readlink")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_readlink(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"mktemp")){
        fprintf(out,"    __b_mktemp();\n"); return;
    }
    if(!strcmp(cmd,"install")){
        if(ac>=3){ char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_install(%s,%s);\n",a,b); free(a); free(b); }
        return;
    }
    /* ---- text processing ---- */
    if(!strcmp(cmd,"tee")){
        const char *files[16]; int nf=0; int append=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-a")) append=1;
            else if(nf<15) files[nf++]=argv[i];
        }
        fprintf(out,"    __b_tee(%d",append);
        for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,",%s",w); free(w); }
        fprintf(out,",NULL);\n");
        return;
    }
    if(!strcmp(cmd,"xargs")){
        /* xargs cmd args... */
        const char *xcmd=NULL; const char *xargs[16]; int xa=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-I")||!strcmp(argv[i],"-0")) continue;
            if(!xcmd) xcmd=argv[i];
            else if(xa<15) xargs[xa++]=argv[i];
        }
        if(xcmd){
            char *cw=emit_word(out,xcmd);
            fprintf(out,"    __b_xargs(%s",cw); free(cw);
            for(int i=0;i<xa;i++){ char *w=emit_word(out,xargs[i]); fprintf(out,",%s",w); free(w); }
            fprintf(out,",NULL);\n");
        }
        return;
    }
    if(!strcmp(cmd,"rev")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_rev(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_rev(NULL);\n");
        return;
    }
    if(!strcmp(cmd,"tac")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_tac(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_tac(NULL);\n");
        return;
    }
    if(!strcmp(cmd,"nl")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_nl(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_nl(NULL);\n");
        return;
    }
    if(!strcmp(cmd,"fold")){
        int w=80; const char *file=NULL;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-w")&&i+1<ac) w=atoi(argv[++i]);
            else file=argv[i];
        }
        char *fw=file?emit_word(out,file):xstrdup("NULL");
        fprintf(out,"    __b_fold(%s,%d);\n",fw,w); free(fw);
        return;
    }
    if(!strcmp(cmd,"paste")){
        const char *files[16]; int nf=0;
        for(int i=1;i<ac;i++) if(nf<15) files[nf++]=argv[i];
        fprintf(out,"    __b_paste(");
        for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"%s,",w); free(w); }
        fprintf(out,"NULL);\n");
        return;
    }
    if(!strcmp(cmd,"expand")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_expand(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_expand(NULL);\n");
        return;
    }
    if(!strcmp(cmd,"unexpand")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_unexpand(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"colrm")||!strcmp(cmd,"column")){
        const char *files[8]; int nf=0;
        for(int i=1;i<ac;i++) if(argv[i][0]!='-'&&nf<7) files[nf++]=argv[i];
        fprintf(out,"    __b_column(");
        for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"%s,",w); free(w); }
        fprintf(out,"NULL);\n");
        return;
    }
    if(!strcmp(cmd,"shuf")){
        const char *files[8]; int nf=0;
        for(int i=1;i<ac;i++) if(argv[i][0]!='-'&&nf<7) files[nf++]=argv[i];
        fprintf(out,"    __b_shuf(");
        for(int i=0;i<nf;i++){ char *w=emit_word(out,files[i]); fprintf(out,"%s,",w); free(w); }
        fprintf(out,"NULL);\n");
        return;
    }
    if(!strcmp(cmd,"comm")){
        if(ac>=3){ char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_comm(%s,%s);\n",a,b); free(a); free(b); }
        return;
    }
    if(!strcmp(cmd,"diff")){
        if(ac>=3){ char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[2]);
            fprintf(out,"    __b_diff(%s,%s);\n",a,b); free(a); free(b); }
        return;
    }
    /* ---- search ---- */
    if(!strcmp(cmd,"find")){
        const char *args[32]; int na=0;
        for(int i=1;i<ac;i++) if(na<31) args[na++]=argv[i];
        fprintf(out,"    __b_find(");
        for(int i=0;i<na;i++){ char *w=emit_word(out,args[i]); fprintf(out,"%s,",w); free(w); }
        fprintf(out,"NULL);\n");
        return;
    }
    if(!strcmp(cmd,"which")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_which(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"whereis")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_whereis(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"locate")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_locate(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"grep")||!strcmp(cmd,"egrep")||!strcmp(cmd,"fgrep")||!strcmp(cmd,"rgrep")){
        /* handled above for grep; egrep/fgrep fall through */
    }
    /* ---- system info ---- */
    if(!strcmp(cmd,"date")){
        const char *fmt=NULL;
        for(int i=1;i<ac;i++){ if(argv[i][0]=='+') fmt=argv[i]; }
        char *fw=fmt?emit_word(out,fmt):xstrdup("NULL");
        fprintf(out,"    __b_date(%s);\n",fw); free(fw);
        return;
    }
    if(!strcmp(cmd,"whoami")){ fprintf(out,"    __b_whoami();\n"); return; }
    if(!strcmp(cmd,"logname")){ fprintf(out,"    __b_whoami();\n"); return; }
    if(!strcmp(cmd,"hostname")){
        if(ac>1){ char *w=emit_word(out,argv[1]); fprintf(out,"    __b_hostname_set(%s);\n",w); free(w); }
        else fprintf(out,"    __b_hostname();\n");
        return;
    }
    if(!strcmp(cmd,"uname")){
        int all=0;
        for(int i=1;i<ac;i++) if(!strcmp(argv[i],"-a")) all=1;
        fprintf(out,"    __b_uname(%d);\n",all);
        return;
    }
    if(!strcmp(cmd,"id")){
        fprintf(out,"    __b_id();\n"); return;
    }
    if(!strcmp(cmd,"env")){
        fprintf(out,"    __b_env();\n"); return;
    }
    if(!strcmp(cmd,"export")){
        for(int i=1;i<ac;i++){
            char *eq=strchr(argv[i],'=');
            if(eq){ *eq=0; char *w=emit_word(out,eq+1);
                fprintf(out,"    setenv(\"%s\",%s,1);\n",argv[i],w);
                fprintf(out,"    strncpy(%s,%s,sizeof(%s)-1);\n",safe_cname(argv[i]),w,safe_cname(argv[i]));
                free(w);
                *eq='=';
            } else {
                fprintf(out,"    setenv(\"%s\",%s,1);\n",argv[i],safe_cname(argv[i]));
            }
        }
        return;
    }
    if(!strcmp(cmd,"unset")){
        for(int i=1;i<ac;i++){
            fprintf(out,"    unsetenv(\"%s\"); %s[0]='\\0';\n",argv[i],safe_cname(argv[i]));
        }
        return;
    }
    if(!strcmp(cmd,"let")){
        /* BUG-11 fix: let x=5+3 → evaluate arithmetic and assign */
        for(int i=1;i<ac;i++){
            const char *expr=argv[i];
            /* find = (not ==) */
            const char *eq=strchr(expr,'=');
            if(eq && eq[1]!='='){
                char vn[128]; int vl=(int)(eq-expr);
                if(vl>=(int)sizeof(vn)) vl=(int)sizeof(vn)-1;
                memcpy(vn,expr,vl); vn[vl]=0;
                if(!is_known_var(vn)) add_var(vn,V_INT);
                /* use translate_arith for the expression part */
                char rhs_expr[256]; int rl=(int)strlen(eq+1);
                if(rl>=(int)sizeof(rhs_expr)) rl=(int)sizeof(rhs_expr)-1;
                memcpy(rhs_expr,eq+1,rl); rhs_expr[rl]=0;
                char *e=translate_arith(rhs_expr);
                VarKind vk=get_var_kind(vn);
                if(vk==V_STR){
                    fprintf(out,"    snprintf(%s,sizeof(%s),\"%%d\",(%s));\n",safe_cname(vn),safe_cname(vn),e);
                } else {
                    fprintf(out,"    %s=(%s);\n",safe_cname(vn),e);
                }
                free(e);
            } else {
                /* let "x++" or "x>5" — evaluate as boolean */
                char *e=translate_arith(expr);
                fprintf(out,"    __exit_status=((%s)!=0);\n",e);
                free(e);
            }
        }
        return;
    }
    if(!strcmp(cmd,"set")){
        /* set -e, set -u, set -- args... */
        fprintf(out,"    /* set");
        for(int i=1;i<ac;i++) fprintf(out," %s",argv[i]);
        fprintf(out," */\n");
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-e")) fprintf(out,"    __sh_set_e=1;\n");
            else if(!strcmp(argv[i],"+e")) fprintf(out,"    __sh_set_e=0;\n");
            else if(!strcmp(argv[i],"-u")) fprintf(out,"    __sh_set_u=1;\n");
            else if(!strcmp(argv[i],"+u")) fprintf(out,"    __sh_set_u=0;\n");
            else if(!strcmp(argv[i],"-x")) fprintf(out,"    __sh_set_x=1;\n");
            else if(!strcmp(argv[i],"+x")) fprintf(out,"    __sh_set_x=0;\n");
            else if(!strcmp(argv[i],"--")){
                /* set positional args */
                int n=0;
                for(int j=i+1;j<ac;j++){ n++;
                    fprintf(out,"    strncpy(__sh_arg%d,%s,sizeof(__sh_arg%d)-1);\n",n,argv[j],n);
                }
                fprintf(out,"    __sh_argc=%d;\n",n);
                break;
            }
        }
        return;
    }
    if(!strcmp(cmd,"source")||!strcmp(cmd,".")){
        if(ac>1){ char *w=emit_word(out,argv[1]);
            fprintf(out,"    __b_source(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"eval")){
        fprintf(out,"    { const char *__ea[]={");
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"%s,",w); free(w); }
        fprintf(out,"NULL}; __b_eval(__ea); }\n");
        return;
    }
    if(!strcmp(cmd,"ps")){
        fprintf(out,"    __b_ps();\n"); return;
    }
    if(!strcmp(cmd,"kill")){
        if(ac>1){ char *w=emit_word(out,argv[1]); fprintf(out,"    __b_kill(%s",w); free(w);
            for(int i=2;i<ac;i++){ char *w2=emit_word(out,argv[i]); fprintf(out,",%s",w2); free(w2); }
            fprintf(out,");\n"); }
        return;
    }
    if(!strcmp(cmd,"sleep")){
        char *w=(ac>1)?emit_word(out,argv[1]):xstrdup("\"1\"");
        fprintf(out,"    { unsigned __sv=atoi(%s); if(strchr(%s,'.'))__sh_usleep((unsigned)(atof(%s)*1000000));else sleep(__sv); }\n",w,w,w);
        free(w);
        return;
    }
    if(!strcmp(cmd,"wait")){
        fprintf(out,"    __b_wait();\n"); return;
    }
    if(!strcmp(cmd,"jobs")){
        fprintf(out,"    __b_jobs();\n"); return;
    }
    if(!strcmp(cmd,"bg")){
        fprintf(out,"    __b_bg();\n"); return;
    }
    if(!strcmp(cmd,"fg")){
        fprintf(out,"    __b_fg();\n"); return;
    }
    if(!strcmp(cmd,"trap")){
        /* trap 'cmd' SIGNAL */
        if(ac>=2){
            char *w=emit_word(out,argv[1]);
            int sig = (ac>=3)?atoi(argv[2]):0;
            fprintf(out,"    __b_trap(%s,%d);\n",w,sig); free(w);
        }
        return;
    }
    if(!strcmp(cmd,"type")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_type(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"command")){
        /* command cmd args... — run without shell function lookup */
        if(ac>1){
            char *cw=emit_word(out,argv[1]);
            fprintf(out,"    __b_command(%s",cw); free(cw);
            for(int i=2;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,",%s",w); free(w); }
            fprintf(out,",NULL);\n");
        }
        return;
    }
    if(!strcmp(cmd,"alias")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_alias(%s);\n",w); free(w); }
        if(ac==1) fprintf(out,"    __b_alias(NULL);\n");
        return;
    }
    if(!strcmp(cmd,"unalias")){
        for(int i=1;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,"    __b_unalias(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"history")){
        fprintf(out,"    __b_history();\n"); return;
    }
    if(!strcmp(cmd,"pushd")){
        if(ac>1){ char *w=emit_word(out,argv[1]); fprintf(out,"    __b_pushd(%s);\n",w); free(w); }
        return;
    }
    if(!strcmp(cmd,"popd")){
        fprintf(out,"    __b_popd();\n"); return;
    }
    if(!strcmp(cmd,"dirs")){
        fprintf(out,"    __b_dirs();\n"); return;
    }
    if(!strcmp(cmd,"seq")){
        int start=1,step=1,end=1;
        if(ac==2) end=atoi(argv[1]);
        else if(ac==3){ start=atoi(argv[1]); end=atoi(argv[2]); }
        else if(ac>=4){ start=atoi(argv[1]); step=atoi(argv[2]); end=atoi(argv[3]); }
        fprintf(out,"    __b_seq(%d,%d,%d);\n",start,step,end);
        return;
    }
    if(!strcmp(cmd,"yes")){
        const char *msg=(ac>1)?argv[1]:"y";
        char *w=emit_word(out,msg);
        fprintf(out,"    __b_yes(%s);\n",w); free(w);
        return;
    }
    if(!strcmp(cmd,"true")){
        fprintf(out,"    __exit_status=0;\n"); return;
    }
    if(!strcmp(cmd,"false")){
        fprintf(out,"    __exit_status=1;\n"); return;
    }
    if(!strcmp(cmd,"test")){
        /* test op args... — reuse cond translator */
        char buf[1024]=""; int bl=0;
        for(int i=1;i<ac;i++){ if(i>1) bl+=snprintf(buf+bl,sizeof(buf)-bl," "); bl+=snprintf(buf+bl,sizeof(buf)-bl,"%s",argv[i]); }
        char *c=translate_cond(buf);
        fprintf(out,"    __exit_status=(%s)?0:1;\n",c);
        return;
    }
    if(!strcmp(cmd,"[")){
        /* [ ... ] — last arg should be ] */
        char buf[1024]="["; int bl=1;
        for(int i=1;i<ac-1;i++){ bl+=snprintf(buf+bl,sizeof(buf)-bl," %s",argv[i]); }
        bl+=snprintf(buf+bl,sizeof(buf)-bl," ]");
        char *c=translate_cond(buf);
        fprintf(out,"    __exit_status=(%s)?0:1;\n",c);
        return;
    }
    if(!strcmp(cmd,"expr")){
        /* simple expr: arg1 op arg2 */
        if(ac>=4){
            char *a=emit_word(out,argv[1]),*b=emit_word(out,argv[3]);
            const char *op=argv[2];
            if(!strcmp(op,"+")) fprintf(out,"    printf(\"%%d\\n\",atoi(%s)+atoi(%s));\n",a,b);
            else if(!strcmp(op,"-")) fprintf(out,"    printf(\"%%d\\n\",atoi(%s)-atoi(%s));\n",a,b);
            else if(!strcmp(op,"\\*")||!strcmp(op,"*")) fprintf(out,"    printf(\"%%d\\n\",atoi(%s)*atoi(%s));\n",a,b);
            else if(!strcmp(op,"/")) fprintf(out,"    printf(\"%%d\\n\",atoi(%s)/atoi(%s));\n",a,b);
            else if(!strcmp(op,"%%")) fprintf(out,"    printf(\"%%d\\n\",atoi(%s)%%atoi(%s));\n",a,b);
            else if(!strcmp(op,"=")) fprintf(out,"    __exit_status=(strcmp(%s,%s)==0)?0:1;\n",a,b);
            else if(!strcmp(op,"!=")) fprintf(out,"    __exit_status=(strcmp(%s,%s)!=0)?0:1;\n",a,b);
            else if(!strcmp(op,"<")) fprintf(out,"    __exit_status=(strcmp(%s,%s)<0)?0:1;\n",a,b);
            else if(!strcmp(op,">")) fprintf(out,"    __exit_status=(strcmp(%s,%s)>0)?0:1;\n",a,b);
            else { fprintf(out,"    printf(\"%%s\\n\",%s);\n",a); }
            free(a); free(b);
        } else if(ac>=2){
            char *a=emit_word(out,argv[1]);
            fprintf(out,"    printf(\"%%s\\n\",%s);\n",a); free(a);
        }
        return;
    }
    if(!strcmp(cmd,"read")){
        const char *vars[8]; int nv=0;
        int read_array=0;
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-r")||!strcmp(argv[i],"-p")) continue;
            if(argv[i][0]=='-'){
                for(const char *fc=argv[i];*fc;fc++) if(*fc=='a') read_array=1;
                continue;
            }
            if(nv<7) vars[nv++]=argv[i];
        }
        if(nv==0) vars[nv++]="REPLY";
        if(read_array){
            add_var(vars[0],V_ARRAY);
            fprintf(out,"    { char __rb[65536]; if(fgets(__rb,sizeof(__rb),stdin)){ int __rl=(int)strlen(__rb); if(__rl>0&&__rb[__rl-1]=='\\n')__rb[--__rl]=0;\n");
            fprintf(out,"    int __ai=0; const char *__ifs=getenv(\"IFS\"); if(!__ifs)__ifs=\" \t\";\n");
            fprintf(out,"    char *__sp=__rb; char *__tok=strtok(__sp,__ifs);\n");
            fprintf(out,"    while(__tok&&__ai<255){ __arr_%s[__ai++]=strdup(__tok); __tok=strtok(NULL,__ifs); } __arr_%s[__ai]=NULL;\n",safe_cname(vars[0]),safe_cname(vars[0]));
            fprintf(out,"    } }\n");
            return;
        }
        fprintf(out,"    { char __rb[65536]; if(fgets(__rb,sizeof(__rb),stdin)){ int __rl=(int)strlen(__rb); if(__rl>0&&__rb[__rl-1]=='\\n')__rb[--__rl]=0;");
        if(nv==1){
            fprintf(out,"    strncpy(%s,__rb,sizeof(%s)-1);",safe_cname(vars[0]),safe_cname(vars[0]));
        } else {
            /* split on whitespace */
            fprintf(out,"    char *__sp=__rb; int __vi=0; char *__tok=strtok(__sp,\" \\t\"); while(__tok&&__vi<%d){",nv);
            fprintf(out,"    switch(__vi){");
            for(int i=0;i<nv;i++) fprintf(out,"    case %d: strncpy(%s,__tok,sizeof(%s)-1); break;",i,safe_cname(vars[i]),safe_cname(vars[i]));
            fprintf(out,"    } __vi++; __tok=strtok(NULL,\" \\t\"); }");
            /* last var gets the rest */
            fprintf(out,"    if(__vi==%d-1 && __tok==NULL){} ",nv);
        }
        fprintf(out,"    } }\n");
        return;
    }
    if(!strcmp(cmd,"exit")){
        if(ac>1){
            char *w=emit_word(out,argv[1]);
            fprintf(out,"    exit(atoi(%s));\n",w); free(w);
        } else fprintf(out,"    exit(__exit_status);\n");
        return;
    }
    if(!strcmp(cmd,"return")){
        if(ac>1){
            char *w=emit_word(out,argv[1]);
            fprintf(out,"    { __exit_status=atoi(%s); return; }\n",w); free(w);
        } else fprintf(out,"    return;\n");
        return;
    }
    if(!strcmp(cmd,"clear")){ fprintf(out,"    fputs(\"\\033[2J\\033[H\",stdout);\n"); return; }
    if(!strcmp(cmd,"reset")){ fprintf(out,"    fputs(\"\\033c\",stdout);\n"); return; }
    if(!strcmp(cmd,"nohup")){
        if(ac>1){
            char *cw=emit_word(out,argv[1]);
            fprintf(out,"    __b_nohup(%s",cw); free(cw);
            for(int i=2;i<ac;i++){ char *w=emit_word(out,argv[i]); fprintf(out,",%s",w); free(w); }
            fprintf(out,",NULL);\n");
        }
        return;
    }
    if(!strcmp(cmd,"time")){
        /* time the rest — emit a wrapper */
        fprintf(out,"    { clock_t __t0=clock();\n");
        /* emit remaining as a sub-call */
        if(ac>1){
            /* build a fake node? simpler: call __b_time with command string */
            fprintf(out,"    __b_time(\"");
            for(int i=1;i<ac;i++){ if(i>1) fprintf(out," "); fprintf(out,"%s",argv[i]); }
            fprintf(out,"\");\n");
        }
        fprintf(out,"    }\n");
        return;
    }
    if(!strcmp(cmd,"free")){
        fprintf(out,"    __b_free_mem();\n"); return;
    }
    if(!strcmp(cmd,"uptime")){
        fprintf(out,"    __b_uptime();\n"); return;
    }
    if(!strcmp(cmd,"who")){
        fprintf(out,"    __b_who();\n"); return;
    }
    if(!strcmp(cmd,"w")){
        fprintf(out,"    __b_who();\n"); return;
    }
    if(!strcmp(cmd,"last")){
        fprintf(out,"    __b_last();\n"); return;
    }
    if(!strcmp(cmd,"dmesg")){
        fprintf(out,"    __b_dmesg();\n"); return;
    }
    if(!strcmp(cmd,"lsof")){
        fprintf(out,"    __b_lsof();\n"); return;
    }
    if(!strcmp(cmd,"mount")){
        fprintf(out,"    __b_mount();\n"); return;
    }
    if(!strcmp(cmd,"umount")){
        fprintf(out,"    /* umount */\n"); return;
    }
    if(!strcmp(cmd,"uname")){
        fprintf(out,"    __b_uname(0);\n"); return;
    }
    if(!strcmp(cmd,"arch")){
        fprintf(out,"    __b_arch();\n"); return;
    }
    if(!strcmp(cmd,"nproc")){
        fprintf(out,"    __b_nproc();\n"); return;
    }
    if(!strcmp(cmd,"tty")){
        fprintf(out,"    __b_tty();\n"); return;
    }
    if(!strcmp(cmd,"stty")){
        fprintf(out,"    /* stty */\n"); return;
    }
    if(!strcmp(cmd,"getopts")){
        /* simple stub */
        fprintf(out,"    /* getopts */\n"); return;
    }
    if(!strcmp(cmd,"hash")){
        fprintf(out,"    /* hash */\n"); return;
    }
    if(!strcmp(cmd,"builtin")){
        if(ac>1){
            /* run argv[1] as builtin */
            char **nargv=argv+1; int nac=ac-1;
            emit_command(out,nargv,nac,id);
        }
        return;
    }
    if(!strcmp(cmd,"help")){
        fprintf(out,"    __b_help();\n"); return;
    }
    if(!strcmp(cmd,"logname")){
        fprintf(out,"    __b_whoami();\n"); return;
    }
    if(!strcmp(cmd,"users")){
        fprintf(out,"    __b_who();\n"); return;
    }
    if(!strcmp(cmd,"domainname")){
        fprintf(out,"    __b_hostname();\n"); return;
    }
    if(!strcmp(cmd,"dnsdomainname")){
        fprintf(out,"    __b_hostname();\n"); return;
    }
    /* ---- additional native builtins for common commands ---- */
    if(!strcmp(cmd,"md5sum")||!strcmp(cmd,"sha1sum")||!strcmp(cmd,"sha256sum")||
       !strcmp(cmd,"sha512sum")){
        /* hash commands — build system() call */
        fprintf(out,"    { char __cmd%d[8192]; int __cl=0;\n",id);
        for(int i=0;i<ac;i++){
            char *w=emit_word(out,argv[i]);
            if(i>0){ fprintf(out,"    __cmd%d[__cl++]=' ';\n",id); }
            fprintf(out,"    __cl+=snprintf(__cmd%d+__cl,sizeof(__cmd%d)-__cl,\"%%s\",%s);\n",id,id,w);
            free(w);
        }
        fprintf(out,"    __exit_status=system(__cmd%d);\n",id);
        fprintf(out,"    if(WIFEXITED(__exit_status))__exit_status=WEXITSTATUS(__exit_status);\n");
        fprintf(out,"    }\n");
        return;
    }
    /* commands that should always use system() passthrough */
    if(!strcmp(cmd,"tar")||!strcmp(cmd,"gzip")||!strcmp(cmd,"gunzip")||
       !strcmp(cmd,"bzip2")||!strcmp(cmd,"bunzip2")||!strcmp(cmd,"xz")||
       !strcmp(cmd,"unxz")||!strcmp(cmd,"zip")||!strcmp(cmd,"unzip")||
       !strcmp(cmd,"7z")||!strcmp(cmd,"rar")||!strcmp(cmd,"unrar")||
       !strcmp(cmd,"curl")||!strcmp(cmd,"wget")||!strcmp(cmd,"ssh")||
       !strcmp(cmd,"scp")||!strcmp(cmd,"rsync")||!strcmp(cmd,"sftp")||
       !strcmp(cmd,"nc")||!strcmp(cmd,"netcat")||!strcmp(cmd,"ping")||
       !strcmp(cmd,"traceroute")||!strcmp(cmd,"dig")||!strcmp(cmd,"nslookup")||
       !strcmp(cmd,"host")||!strcmp(cmd,"ifconfig")||!strcmp(cmd,"ip")||
       !strcmp(cmd,"route")||!strcmp(cmd,"iptables")||!strcmp(cmd,"netstat")||
       !strcmp(cmd,"ss")||!strcmp(cmd,"lsof")||!strcmp(cmd,"tcpdump")||
       !strcmp(cmd,"awk")||!strcmp(cmd,"gawk")||!strcmp(cmd,"mawk")||
       !strcmp(cmd,"perl")||!strcmp(cmd,"python")||!strcmp(cmd,"python3")||
       !strcmp(cmd,"ruby")||!strcmp(cmd,"node")||!strcmp(cmd,"php")||
       !strcmp(cmd,"java")||!strcmp(cmd,"javac")||!strcmp(cmd,"go")||
       !strcmp(cmd,"rustc")||!strcmp(cmd,"cargo")||!strcmp(cmd,"make")||
       !strcmp(cmd,"cmake")||!strcmp(cmd,"gcc")||!strcmp(cmd,"g++")||
       !strcmp(cmd,"cc")||!strcmp(cmd,"ld")||!strcmp(cmd,"ar")||
       !strcmp(cmd,"ranlib")||!strcmp(cmd,"strip")||!strcmp(cmd,"objdump")||
       !strcmp(cmd,"nm")||!strcmp(cmd,"readelf")||!strcmp(cmd,"hexdump")||
       !strcmp(cmd,"xxd")||!strcmp(cmd,"od")||!strcmp(cmd,"strings")||
       !strcmp(cmd,"top")||!strcmp(cmd,"htop")||!strcmp(cmd,"iotop")||
       !strcmp(cmd,"vmstat")||!strcmp(cmd,"iostat")||!strcmp(cmd,"sar")||
       !strcmp(cmd,"mpstat")||!strcmp(cmd,"lscpu")||!strcmp(cmd,"lspci")||
       !strcmp(cmd,"lsusb")||!strcmp(cmd,"lsblk")||!strcmp(cmd,"lsmem")||
       !strcmp(cmd,"lsmod")||!strcmp(cmd,"lsns")||!strcmp(cmd,"lsof")||
       !strcmp(cmd,"fdisk")||!strcmp(cmd,"parted")||!strcmp(cmd,"mkfs")||
       !strcmp(cmd,"fsck")||!strcmp(cmd,"mount")||!strcmp(cmd,"umount")||
       !strcmp(cmd,"dd")||!strcmp(cmd,"sync")||!strcmp(cmd,"blkid")||
       !strcmp(cmd,"smartctl")||!strcmp(cmd,"hdparm")||!strcmp(cmd,"sdparm")||
       !strcmp(cmd,"systemctl")||!strcmp(cmd,"service")||!strcmp(cmd,"init")||
       !strcmp(cmd,"shutdown")||!strcmp(cmd,"reboot")||!strcmp(cmd,"halt")||
       !strcmp(cmd,"poweroff")||!strcmp(cmd,"login")||!strcmp(cmd,"logout")||
       !strcmp(cmd,"su")||!strcmp(cmd,"sudo")||!strcmp(cmd,"visudo")||
       !strcmp(cmd,"passwd")||!strcmp(cmd,"useradd")||!strcmp(cmd,"userdel")||
       !strcmp(cmd,"usermod")||!strcmp(cmd,"groupadd")||!strcmp(cmd,"groupdel")||
       !strcmp(cmd,"groupmod")||!strcmp(cmd,"gpasswd")||!strcmp(cmd,"chage")||
       !strcmp(cmd,"chsh")||!strcmp(cmd,"chfn")||!strcmp(cmd,"newgrp")||
       !strcmp(cmd,"crontab")||!strcmp(cmd,"at")||!strcmp(cmd,"batch")||
       !strcmp(cmd,"anacron")||!strcmp(cmd,"logger")||!strcmp(cmd,"journalctl")||
       !strcmp(cmd,"dmesg")||!strcmp(cmd,"last")||!strcmp(cmd,"lastlog")||
       !strcmp(cmd,"who")||!strcmp(cmd,"w")||!strcmp(cmd,"finger")||
       !strcmp(cmd,"write")||!strcmp(cmd,"wall")||!strcmp(cmd,"mesg")||
       !strcmp(cmd,"talk")||!strcmp(cmd,"ytalk")||!strcmp(cmd,"screen")||
       !strcmp(cmd,"tmux")||!strcmp(cmd,"byobu")||!strcmp(cmd,"dialog")||
       !strcmp(cmd,"whiptail")||!strcmp(cmd,"zenity")||!strcmp(cmd,"kdialog")||
       !strcmp(cmd,"xterm")||!strcmp(cmd,"gnome-terminal")||!strcmp(cmd,"konsole")||
       !strcmp(cmd,"vi")||!strcmp(cmd,"vim")||!strcmp(cmd,"emacs")||
       !strcmp(cmd,"nano")||!strcmp(cmd,"pico")||!strcmp(cmd,"ed")||
       !strcmp(cmd,"sed")||!strcmp(cmd,"less")||!strcmp(cmd,"more")||
       !strcmp(cmd,"cat")||!strcmp(cmd,"tac")||!strcmp(cmd,"nl")||
       !strcmp(cmd,"head")||!strcmp(cmd,"tail")||!strcmp(cmd,"cut")||
       !strcmp(cmd,"paste")||!strcmp(cmd,"join")||!strcmp(cmd,"comm")||
       !strcmp(cmd,"uniq")||!strcmp(cmd,"sort")||!strcmp(cmd,"shuf")||
       !strcmp(cmd,"fold")||!strcmp(cmd,"fmt")||!strcmp(cmd,"pr")||
       !strcmp(cmd,"column")||!strcmp(cmd,"expand")||!strcmp(cmd,"unexpand")||
       !strcmp(cmd,"rev")||!strcmp(cmd,"tr")||!strcmp(cmd,"grep")||
       !strcmp(cmd,"egrep")||!strcmp(cmd,"fgrep")||!strcmp(cmd,"rgrep")||
       !strcmp(cmd,"rg")||!strcmp(cmd,"ag")||!strcmp(cmd,"ack")||
       !strcmp(cmd,"find")||!strcmp(cmd,"locate")||!strcmp(cmd,"updatedb")||
       !strcmp(cmd,"which")||!strcmp(cmd,"whereis")||!strcmp(cmd,"type")||
       !strcmp(cmd,"file")||!strcmp(cmd,"stat")||!strcmp(cmd,"du")||
       !strcmp(cmd,"df")||!strcmp(cmd,"free")||!strcmp(cmd,"uptime")||
       !strcmp(cmd,"uname")||!strcmp(cmd,"hostname")||!strcmp(cmd,"arch")||
       !strcmp(cmd,"nproc")||!strcmp(cmd,"tty")||!strcmp(cmd,"stty")||
       !strcmp(cmd,"reset")||!strcmp(cmd,"clear")||!strcmp(cmd,"tput")||
       !strcmp(cmd,"infocmp")||!strcmp(cmd,"tic")||!strcmp(cmd,"toe")||
       !strcmp(cmd,"captoinfo")||!strcmp(cmd,"tabs")||!strcmp(cmd,"tset")||
       !strcmp(cmd,"lock")||!strcmp(cmd,"vlock")||!strcmp(cmd,"xlock")||
       !strcmp(cmd,"chvt")||!strcmp(cmd,"deallocvt")||!strcmp(cmd,"openvt")||
       !strcmp(cmd,"deallocvt")||!strcmp(cmd,"fgconsole")||!strcmp(cmd,"fg")||
       !strcmp(cmd,"bg")||!strcmp(cmd,"jobs")||!strcmp(cmd,"disown")||
       !strcmp(cmd,"nohup")||!strcmp(cmd,"timeout")||!strcmp(cmd,"nice")||
       !strcmp(cmd,"renice")||!strcmp(cmd,"ionice")||!strcmp(cmd,"taskset")||
       !strcmp(cmd,"chrt")||!strcmp(cmd,"ulimit")||!strcmp(cmd,"umask")||
       !strcmp(cmd,"env")||!strcmp(cmd,"printenv")||!strcmp(cmd,"export")||
       !strcmp(cmd,"set")||!strcmp(cmd,"unset")||!strcmp(cmd,"source")||
       !strcmp(cmd,"eval")||!strcmp(cmd,"exec")||!strcmp(cmd,"command")||
       !strcmp(cmd,"builtin")||!strcmp(cmd,"type")||!strcmp(cmd,"hash")||
       !strcmp(cmd,"alias")||!strcmp(cmd,"unalias")||!strcmp(cmd,"history")||
       !strcmp(cmd,"fc")||!strcmp(cmd,"read")||!strcmp(cmd,"mapfile")||
       !strcmp(cmd,"readarray")||!strcmp(cmd,"getopts")||!strcmp(cmd,"getopt")||
       !strcmp(cmd,"select")||!strcmp(cmd,"complete")||!strcmp(cmd,"compgen")||
       !strcmp(cmd,"compopt")||!strcmp(cmd,"declare")||!strcmp(cmd,"typeset")||
       !strcmp(cmd,"local")||!strcmp(cmd,"readonly")||!strcmp(cmd,"export")||
       !strcmp(cmd,"trap")||!strcmp(cmd,"return")||!strcmp(cmd,"break")||
       !strcmp(cmd,"continue")||!strcmp(cmd,"exit")||!strcmp(cmd,"logout")||
       !strcmp(cmd,"suspend")||!strcmp(cmd,"kill")||!strcmp(cmd,"killall")||
       !strcmp(cmd,"pkill")||!strcmp(cmd,"pgrep")||!strcmp(cmd,"pidof")||
       !strcmp(cmd,"wait")||!strcmp(cmd,"disown")||!strcmp(cmd,"coproc")||
       !strcmp(cmd,"time")||!strcmp(cmd,"times")||!strcmp(cmd,"pwd")||
       !strcmp(cmd,"cd")||!strcmp(cmd,"pushd")||!strcmp(cmd,"popd")||
       !strcmp(cmd,"dirs")||!strcmp(cmd,"ls")||!strcmp(cmd,"dir")||
       !strcmp(cmd,"vdir")||!strcmp(cmd,"cp")||!strcmp(cmd,"mv")||
       !strcmp(cmd,"rm")||!strcmp(cmd,"mkdir")||!strcmp(cmd,"rmdir")||
       !strcmp(cmd,"ln")||!strcmp(cmd,"link")||!strcmp(cmd,"unlink")||
       !strcmp(cmd,"touch")||!strcmp(cmd,"mktemp")||!strcmp(cmd,"install")||
       !strcmp(cmd,"chmod")||!strcmp(cmd,"chown")||!strcmp(cmd,"chgrp")||
       !strcmp(cmd,"chroot")||!strcmp(cmd,"chcon")||!strcmp(cmd,"restorecon")||
       !strcmp(cmd,"setfacl")||!strcmp(cmd,"getfacl")||!strcmp(cmd,"attr")||
       !strcmp(cmd,"lsattr")||!strcmp(cmd,"chattr")||!strcmp(cmd,"lsblk")||
       !strcmp(cmd,"blkid")||!strcmp(cmd,"findmnt")||!strcmp(cmd,"fuser")||
       !strcmp(cmd,"lsof")||!strcmp(cmd,"strace")||!strcmp(cmd,"ltrace")||
       !strcmp(cmd,"gdb")||!strcmp(cmd,"lldb")||!strcmp(cmd,"perf")||
       !strcmp(cmd,"valgrind")||!strcmp(cmd,"callgrind")||!strcmp(cmd,"memcheck")||
       !strcmp(cmd,"massif")||!strcmp(cmd,"helgrind")||!strcmp(cmd,"drd")||
       !strcmp(cmd,"addr2line")||!strcmp(cmd,"c++filt")||!strcmp(cmd,"elfedit")||
       !strcmp(cmd,"patch")||!strcmp(cmd,"diff")||!strcmp(cmd,"diff3")||
       !strcmp(cmd,"sdiff")||!strcmp(cmd,"cmp")||!strcmp(cmd,"comm")||
       !strcmp(cmd,"patch")||!strcmp(cmd,"rsync")||!strcmp(cmd,"scp")||
       !strcmp(cmd,"sftp")||!strcmp(cmd,"ftp")||!strcmp(cmd,"lftp")||
       !strcmp(cmd,"tftp")||!strcmp(cmd,"ncftp")||!strcmp(cmd,"wget")||
       !strcmp(cmd,"curl")||!strcmp(cmd,"aria2c")||!strcmp(cmd,"axel")||
       !strcmp(cmd,"git")||!strcmp(cmd,"svn")||!strcmp(cmd,"hg")||
       !strcmp(cmd,"bzr")||!strcmp(cmd,"cvs")||!strcmp(cmd,"rcs")||
       !strcmp(cmd,"docker")||!strcmp(cmd,"docker-compose")||!strcmp(cmd,"podman")||
       !strcmp(cmd,"kubectl")||!strcmp(cmd,"helm")||!strcmp(cmd,"minikube")||
       !strcmp(cmd,"oc")||!strcmp(cmd,"containerd")||!strcmp(cmd,"ctr")||
       !strcmp(cmd,"buildah")||!strcmp(cmd,"skopeo")||!strcmp(cmd,"crictl")||
       !strcmp(cmd,"ansible")||!strcmp(cmd,"ansible-playbook")||!strcmp(cmd,"terraform")||
       !strcmp(cmd,"packer")||!strcmp(cmd,"vagrant")||!strcmp(cmd,"puppet")||
       !strcmp(cmd,"chef")||!strcmp(cmd,"salt")||!strcmp(cmd,"saltstack")){
        /* All these commands use system() passthrough */
        fprintf(out,"    { char __cmd%d[8192]; int __cl=0;\n",id);
        for(int i=0;i<ac;i++){
            char *w=emit_word(out,argv[i]);
            if(i>0){ fprintf(out,"    __cmd%d[__cl++]=' ';\n",id); }
            fprintf(out,"    __cl+=snprintf(__cmd%d+__cl,sizeof(__cmd%d)-__cl,\"%%s\",%s);\n",id,id,w);
            free(w);
        }
        fprintf(out,"    __exit_status=system(__cmd%d);\n",id);
        fprintf(out,"    if(WIFEXITED(__exit_status))__exit_status=WEXITSTATUS(__exit_status);\n");
        fprintf(out,"    }\n");
        return;
    }
    /* ---- fallback: user-defined function call OR system command ---- */
    {
        if(is_user_func(cmd)){
            /* user-defined function call — use stack array for small arg counts */
            int nfa=ac-1;
            if(nfa<=8){
                /* Stack-allocated: no malloc/strdup/free overhead */
                fprintf(out,"    {\n");
                fprintf(out,"    char *__fa%d[%d];\n",id,nfa+1);
                for(int i=0;i<nfa;i++){
                    char *w=emit_word(out,argv[i+1]);
                    fprintf(out,"    __fa%d[%d] = (char*)%s;\n",id,i,w);
                    free(w);
                }
                fprintf(out,"    __fa%d[%d] = NULL;\n",id,nfa);
                fprintf(out,"    %s(%d, __fa%d);\n",safe_cname(cmd),nfa,id);
                fprintf(out,"    }\n");
            } else {
                /* Heap-allocated for large arg counts */
                fprintf(out,"    {\n");
                fprintf(out,"    char **__fa%d = (char**)malloc(%d * sizeof(char*));\n",id,nfa+1);
                for(int i=0;i<nfa;i++){
                    char *w=emit_word(out,argv[i+1]);
                    fprintf(out,"    __fa%d[%d] = strdup(%s);\n",id,i,w);
                    free(w);
                }
                fprintf(out,"    __fa%d[%d] = NULL;\n",id,nfa);
                fprintf(out,"    %s(%d, __fa%d);\n",safe_cname(cmd),nfa,id);
                fprintf(out,"    __sh_arr_free(__fa%d);\n",id);
                fprintf(out,"    }\n");
            }
        } else {
            /* system command via system() — supports ALL system commands */
            /* Check for bash -c / sh -c pattern: need to quote the script argument */
            int is_bash_c = (ac>=3 && !strcmp(argv[0],"bash") && !strcmp(argv[1],"-c"));
            int is_sh_c   = (ac>=3 && !strcmp(argv[0],"sh") && !strcmp(argv[1],"-c"));
            fprintf(out,"    { char __cmd%d[8192]; int __cl=0;\n",id);
            for(int i=0;i<ac;i++){
                if(i>0){ fprintf(out,"    __cmd%d[__cl++]=' ';\n",id); }
                /* For bash -c / sh -c, the script argument (i>=2) needs quoting */
                if((is_bash_c||is_sh_c) && i>=2){
                    /* Quote the argument to preserve it as a single shell string */
                    char *w=emit_word(out,argv[i]);
                    fprintf(out,"    __cmd%d[__cl++]='\\'';\n",id);
                    fprintf(out,"    __cl+=snprintf(__cmd%d+__cl,sizeof(__cmd%d)-__cl,\"%%s\",%s);\n",id,id,w);
                    fprintf(out,"    __cmd%d[__cl++]='\\'';\n",id);
                    free(w);
                } else {
                    char *w=emit_word(out,argv[i]);
                    fprintf(out,"    __cl+=snprintf(__cmd%d+__cl,sizeof(__cmd%d)-__cl,\"%%s\",%s);\n",id,id,w);
                    free(w);
                }
            }
            fprintf(out,"    __exit_status=system(__cmd%d);\n",id);
            fprintf(out,"    if(WIFEXITED(__exit_status))__exit_status=WEXITSTATUS(__exit_status);\n");
            fprintf(out,"    }\n");
        }
        return;
    }
    /* ---- old fallback: user-defined function call ---- */
    {
        int nfa=ac-1;
        fprintf(out,"    {\n");
        fprintf(out,"    char **__fa%d=(char**)malloc(%d*sizeof(char*));\n",id,nfa+1);
        for(int i=0;i<nfa;i++){
            char *w=emit_word(out,argv[i+1]);
            fprintf(out,"    __fa%d[%d]=strdup(%s);\n",id,i,w);
            free(w);
        }
        fprintf(out,"    __fa%d[%d]=NULL;\n",id,nfa);
        fprintf(out,"    %s(%d,__fa%d);\n",safe_cname(cmd),nfa,id);
        fprintf(out,"    for(int __fi=0;__fi<%d;__fi++)free(__fa%d[__fi]);\n",nfa,id);
        fprintf(out,"    free(__fa%d);\n",id);
        fprintf(out,"    }\n");
    }
}

void emit_node(FILE *out, Node *n){
    if(!n) return;
    switch(n->type){

    case NODE_ASSIGN:{
        /* check for array element assignment (lineno=-3) */
        if(n->lineno==-3 && n->rhs){
            /* arr[key]=value — rhs is "key\x01value" */
            add_var(n->lhs,V_ARRAY);
            const char *sep=strchr(n->rhs,'\x01');
            char akey[256]; char aval[1024];
            if(sep){
                int kl=(int)(sep-n->rhs);
                if(kl>=255) kl=255;
                memcpy(akey,n->rhs,kl); akey[kl]=0;
                strncpy(aval,sep+1,sizeof(aval)-1); aval[sizeof(aval)-1]=0;
            } else {
                strncpy(akey,n->rhs,sizeof(akey)-1); akey[sizeof(akey)-1]=0;
                aval[0]=0;
            }
            /* if key is numeric, use as index; otherwise use assoc set */
            int is_num_key=1;
            for(const char *q=akey;*q;q++){ if(!isdigit((unsigned char)*q)){ is_num_key=0; break; } }
            /* BUG: if key is $var, it's a runtime-numeric index */
            int is_var_key=(akey[0]=='$' && akey[1] && akey[1]!='{');
            if(is_var_key) is_num_key=1; /* treat $var as numeric index */
            if(is_num_key && akey[0]){
                if(is_var_key){
                    /* Runtime evaluation: use atoi(var) as index */
                    char *e=translate_expr(akey);
                    /* Check if var is string type — need atoi */
                    const char *vname=akey+1;
                    if(*vname=='{') vname++;
                    VarKind vk=get_var_kind(vname);
                    if(vk==V_STR)
                        fprintf(out,"    { int __ai=0; while(__arr_%s[__ai]) __ai++;\n",safe_cname(n->lhs));
                    else
                        fprintf(out,"    { int __ai=0; while(__arr_%s[__ai]) __ai++;\n",safe_cname(n->lhs));
                    if(vk==V_STR)
                        fprintf(out,"    int __idx=atoi(%s); if(__idx<0)__idx=0; if(__idx>=256)__idx=255;\n",e);
                    else
                        fprintf(out,"    int __idx=%s; if(__idx<0)__idx=0; if(__idx>=256)__idx=255;\n",e);
                    free(e);
                    fprintf(out,"    __arr_%s[__idx]=",safe_cname(n->lhs));
                }else{
                fprintf(out,"    { int __ai=0; while(__arr_%s[__ai]) __ai++;\n",safe_cname(n->lhs));
                fprintf(out,"    int __idx=atoi(\"%s\");\n",akey);
                fprintf(out,"    if(__idx>=256) __idx=255;\n");
                fprintf(out,"    __arr_%s[__idx]=",safe_cname(n->lhs));
                }
            } else {
                /* BUG-10 fix: use __sh_assoc_set for string keys */
                fprintf(out,"    __sh_assoc_set(__arr_%s,\"%s\",",safe_cname(n->lhs),akey);
            }
            /* emit value */
            if(aval[0]=='"'){
                char inner[1024]; int il=0;
                const char *vp=aval+1;
                while(*vp && *vp!='"' && il<(int)sizeof(inner)-1) inner[il++]=*vp++;
                inner[il]=0;
                if(strchr(inner,'$')){
                    ExpandResult er; expand_string(inner,&er);
                    if(is_num_key && akey[0])
                        fprintf(out,"__sh_fmt(\"%s\"",er.fmt);
                    else
                        fprintf(out,"strdup(__sh_fmt(\"%s\"",er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    if(is_num_key && akey[0])
                        fprintf(out,"));\n");
                    else
                        fprintf(out,")));\n");
                    expand_free(&er);
                } else {
                    if(is_num_key && akey[0])
                        fprintf(out,"strdup(\"%s\");\n",inner);
                    else
                        fprintf(out,"strdup(\"%s\"));\n",inner);
                }
            } else if(strchr(aval,'$')){
                ExpandResult er; expand_string(aval,&er);
                fprintf(out,"strdup(__sh_fmt(\"%s\"",er.fmt);
                for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                if(is_num_key && akey[0])
                    fprintf(out,"));\n");
                else
                    fprintf(out,"));\n");
                expand_free(&er);
            } else {
                if(is_num_key && akey[0])
                    fprintf(out,"strdup(\"%s\");\n",aval);
                else
                    fprintf(out,"strdup(\"%s\"));\n",aval);
            }
            /* close brace only for numeric key path (which opened a block with {) */
            if(is_num_key && akey[0])
                fprintf(out,"    }\n");
            break;
        }
        /* check for append (lineno=-1 for array, -2 for string) */
        if(n->lineno==-1 && n->rhs && n->rhs[0]=='('){
            /* array append: arr+=(elem1 elem2) */
            add_var(n->lhs,V_ARRAY);
            const char *p=n->rhs+1;
            /* find current array length */
            fprintf(out,"    { int __al=0; while(__arr_%s[__al]) __al++;\n",safe_cname(n->lhs));
            while(*p && *p!=')'){
                while(*p==' '||*p=='\t') p++;
                if(*p==')') break;
                char elem[256]; int ei=0;
                if(*p=='"'){ p++; while(*p&&*p!='"'&&ei<(int)sizeof(elem)-1){
                    if(*p=='\\' && *(p+1)){
                        char next=*(p+1);
                        if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n') p++; /* skip backslash */
                    }
                    elem[ei++]=*p++;
                } if(*p=='"')p++; }
                else if(*p=='\''){ p++; while(*p&&*p!='\''&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='\'')p++; }
                else { while(*p&&*p!=' '&&*p!='\t'&&*p!=')'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; }
                elem[ei]=0;
                { char *esc=c_escape_literal(elem);
                fprintf(out,"    __arr_%s[__al++]=\"%s\";\n",safe_cname(n->lhs),esc);
                free(esc); }
            }
            fprintf(out,"    __arr_%s[__al]=NULL; }\n",safe_cname(n->lhs));
            break;
        }
        if(n->lineno==-4){
            /* arithmetic compound assign: x = x op rhs */
            add_var(n->lhs,V_INT);
            const char *cn=safe_cname(n->lhs);
            char *e=translate_arith(n->rhs);
            fprintf(out,"    %s=(%s);\n",cn,e);
            free(e);
            break;
        }
        if(n->lineno==-2){
            /* string append: var+=value */
            /* BUG-14 fix: if var is V_INT, use arithmetic += instead */
            VarKind vk=get_var_kind(n->lhs);
            if(vk==V_INT){
                add_var(n->lhs,V_INT);
                const char *cn=safe_cname(n->lhs);
                char *e=translate_arith(n->rhs);
                fprintf(out,"    %s+=(%s);\n",cn,e);
                free(e);
                break;
            }
            add_var(n->lhs,V_STR);
            const char *cn=safe_cname(n->lhs);
            if(strchr(n->rhs,'$')){
                ExpandResult er; expand_string(n->rhs,&er);
                fprintf(out,"    strncat(%s,",cn);
                /* build temp string */
                int id=tmp_id++;
                fprintf(out,"(snprintf(__tw_%d,sizeof(__tw_%d),\"%s\"",id,id,er.fmt);
                for(int i=0;i<er.nargs;i++) fprintf(out,",%s",er.args[i]);
                fprintf(out,"),__tw_%d)",id);
                fprintf(out,",sizeof(%s)-strlen(%s)-1);\n",cn,cn);
                expand_free(&er);
            } else {
                /* BUG-06 fix: strip surrounding quotes from rhs before escaping */
                const char *val=n->rhs;
                int vlen=(int)strlen(val);
                char *inner_str=NULL;
                if(vlen>=2 && ((val[0]=='"'&&val[vlen-1]=='"') || (val[0]=='\''&&val[vlen-1]=='\''))){
                    inner_str=malloc(vlen-1);
                    memcpy(inner_str,val+1,vlen-2);
                    inner_str[vlen-2]=0;
                    val=inner_str;
                }
                char *esc=c_escape_literal(val);
                fprintf(out,"    strncat(%s,\"%s\",sizeof(%s)-strlen(%s)-1);\n",cn,esc,cn,cn);
                free(esc);
                free(inner_str);
            }
            break;
        }
        if(n->rhs && n->rhs[0]=='('){
            /* array */
            add_var(n->lhs,V_ARRAY);
            const char *p=n->rhs+1;
            char items[128][256]; int ni=0;
            while(*p && *p!=')'){
                while(*p==' '||*p=='\t') p++;
                if(*p==')') break;
                char *q=items[ni]; int k=0;
                int saw_quotes=0; /* track quoted elements (including empty "") */
                while(*p && *p!=' ' && *p!='\t' && *p!=')'){
                    if(*p=='"'){ p++; saw_quotes=1; while(*p&&*p!='"'){
                        /* In bash double quotes, \ is only special before $ ` " \ and newline.
                         * Before other chars, \ is literal. */
                        if(*p=='\\' && *(p+1)){
                            char next=*(p+1);
                            if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n'){
                                p++; /* skip backslash, keep next char */
                            }
                            /* else: keep backslash literally */
                        }
                        q[k++]=*p++;
                    } if(*p)p++; }
                    else if(*p=='\''){ p++; saw_quotes=1; while(*p&&*p!='\'') q[k++]=*p++; if(*p)p++; }
                    else if(*p=='$' && (*(p+1)=='{' || *(p+1)=='(')){
                        /* ${...} or $(...) — read as single unit */
                        char open=*(p+1); char close=(open=='{')?'}':')';
                        q[k++]=*p++;
                        q[k++]=*p++;
                        int d=1;
                        while(*p && d && k<255){
                            if(*p==open) d++;
                            else if(*p==close) d--;
                            q[k++]=*p++;
                        }
                    }
                    else q[k++]=*p++;
                }
                q[k]=0;
                /* Always add element if it was quoted (even if empty) or non-empty */
                if(k>0 || saw_quotes){ ni++; }
            }
            fprintf(out,"    /* array %s */\n",safe_cname(n->lhs));
            fprintf(out,"    {\n");
            fprintf(out,"    int __an%d=0;\n",n->lineno);
            for(int i=0;i<ni;i++){
                /* Check if item is an unquoted $... expansion — needs word-splitting */
                if(items[i][0]=='$' && items[i][0]!='"' && items[i][0]!='\''){
                    /* Unquoted variable expansion — word-split at runtime */
                    ExpandResult er; expand_string(items[i],&er);
                    int id=tmp_id++;
                    fprintf(out,"    { char __tw_%d[65536]; snprintf(__tw_%d,sizeof(__tw_%d),\"%s\"",id,id,id,er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    fprintf(out,");\n");
                    expand_free(&er);
                    fprintf(out,"    char *__sp=__tw_%d;\n",id);
                    /* BUG-12 fix: use IFS for word splitting */
                    fprintf(out,"    const char *__ifs=getenv(\"IFS\"); if(!__ifs)__ifs=\" \\t\\n\";\n");
                    fprintf(out,"    while(*__sp){ while(*__sp&&strchr(__ifs,*__sp))__sp++; if(!*__sp)break;\n");
                    fprintf(out,"    char *__se=__sp; while(*__se&&!strchr(__ifs,*__se))__se++;\n");
                    fprintf(out,"    char __sc=*__se; *__se=0; __arr_%s[__an%d++]=strdup(__sp); *__se=__sc; __sp=__se; if(*__sp)__sp++; }\n",safe_cname(n->lhs),n->lineno);
                    fprintf(out,"    }\n");
                } else if(strchr(items[i],'$')){
                    /* Quoted or mixed item with $ — expand as single element */
                    ExpandResult er; expand_string(items[i],&er);
                    int id=tmp_id++;
                    fprintf(out,"    char __tw_%d[65536]; snprintf(__tw_%d,sizeof(__tw_%d),\"%s\"",id,id,id,er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    fprintf(out,");\n");
                    expand_free(&er);
                    fprintf(out,"    __arr_%s[__an%d++]=strdup(__tw_%d);\n",safe_cname(n->lhs),n->lineno,id);
                } else {
                    char *esc = c_escape_literal(items[i]);
                    /* BUG-19 fix: detect glob patterns and expand at runtime */
                    int has_glob=0;
                    for(const char *g=items[i];*g;g++){
                        if(*g=='*'||*g=='?'||*g=='['){ has_glob=1; break; }
                    }
                    if(has_glob){
                        /* emit glob expansion */
                        fprintf(out,"    { glob_t __g; if(glob(\"%s\",0,NULL,&__g)==0){ for(unsigned __gi=0;__gi<__g.gl_pathc&&__an%d<255;__gi++) __arr_%s[__an%d++]=strdup(__g.gl_pathv[__gi]); } globfree(&__g); }\n",esc,n->lineno,safe_cname(n->lhs),n->lineno);
                    } else {
                        fprintf(out,"    __arr_%s[__an%d++]=strdup(\"%s\");\n",safe_cname(n->lhs),n->lineno,esc);
                    }
                    free(esc);
                }
            }
            fprintf(out,"    __arr_%s[__an%d]=NULL;\n",safe_cname(n->lhs),n->lineno);
            fprintf(out,"    }\n");
        } else if(n->rhs && strncmp(n->rhs,"$((",3)==0){
            add_var(n->lhs,V_INT);
            char *e=translate_expr(n->rhs);
            fprintf(out,"    %s=%s;\n",safe_cname(n->lhs),e); free(e);
        } else if(n->rhs && strncmp(n->rhs,"$(",2)==0){
            /* command substitution → string */
            add_var(n->lhs,V_STR);
            char *e=translate_expr(n->rhs);
            fprintf(out,"    strncpy(%s,%s,sizeof(%s)-1);\n",safe_cname(n->lhs),e,safe_cname(n->lhs));
            free(e);
        } else {
            char *rhs=n->rhs?n->rhs:"";
            int isnum=1;
            const char *rc=rhs;
            if(*rc=='-'||*rc=='+') rc++;
            if(!*rc) isnum=0;
            while(*rc){ if(!isdigit((unsigned char)*rc)){isnum=0;break;} rc++; }
            if(isnum && rhs[0]!='\0'){
                VarKind vk=get_var_kind(n->lhs);
                if(vk==V_INT){
                    add_var(n->lhs,V_INT);
                    fprintf(out,"    %s=%s;\n",safe_cname(n->lhs),rhs);
                } else {
                    /* string variable assigned a number — use snprintf */
                    add_var(n->lhs,V_STR);
                    fprintf(out,"    snprintf(%s,sizeof(%s),\"%%s\",\"%s\");\n",
                            safe_cname(n->lhs),safe_cname(n->lhs),rhs);
                }
            } else if(strchr(rhs,'$')){
                ExpandResult er; expand_string(rhs,&er);
                add_var(n->lhs,V_STR);
                fprintf(out,"    snprintf(%s,sizeof(%s),\"%s\"",
                        safe_cname(n->lhs),safe_cname(n->lhs),er.fmt);
                for(int i=0;i<er.nargs;i++) fprintf(out,",%s",er.args[i]);
                fprintf(out,");\n");
                expand_free(&er);
            } else {
                add_var(n->lhs,V_STR);
                const char *val=rhs;
                char tmp[2048];
                if((val[0]=='"'||val[0]=='\'')&&strlen(val)>=2){
                    char q=val[0];
                    const char *vp=val+1;
                    int ti=0;
                    while(*vp && *vp!=q && ti<(int)sizeof(tmp)-1){
                        if(q=='"' && *vp=='\\' && *(vp+1)){
                            char next=*(vp+1);
                            if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n') vp++;
                        }
                        tmp[ti++]=*vp++;
                    }
                    tmp[ti]=0;
                    char *esc=c_escape_literal(tmp);
                    fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",
                            safe_cname(n->lhs),esc,safe_cname(n->lhs));
                    /* BUG-12 fix: sync IFS and other special vars to environment */
                    if(!strcmp(n->lhs,"IFS"))
                        fprintf(out,"    setenv(\"IFS\",\"%s\",1);\n",esc);
                    free(esc);
                } else {
                    char *esc=c_escape_literal(val);
                    fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",
                            safe_cname(n->lhs),esc,safe_cname(n->lhs));
                    /* sync IFS to environment */
                    if(!strcmp(n->lhs,"IFS"))
                        fprintf(out,"    setenv(\"IFS\",\"%s\",1);\n",esc);
                    free(esc);
                }
            }
        }
        break;
    }

    case NODE_LOCAL:{
        /* local/declare var=val — handle int, str, array types */
        for(int i=0;i<n->argc;i++){
            char *eq=strchr(n->argv[i],'=');
            if(eq){ *eq=0;
                const char *cn=safe_cname(n->argv[i]);
                VarKind vk=get_var_kind(n->argv[i]);
                if(vk==V_INT){
                    /* int assignment — use numeric operand translation */
                    char *e=translate_num_operand(eq+1);
                    fprintf(out,"    %s=%s;\n",cn,e);
                    free(e);
                } else if(vk==V_ARRAY || eq[1]=='('){
                    /* array assignment */
                    add_var(n->argv[i],V_ARRAY);
                    /* parse array elements */
                    const char *p=eq+2; /* skip =( */
                    int idx=0;
                    /* Check for ("$@") or ("$*") — expand args */
                    if(*p=='"' && ((*(p+1)=='$' && (*(p+2)=='@'||*(p+2)=='*')) || (*(p+1)=='$' && *(p+2)=='{' && (*(p+3)=='@'||*(p+3)=='*')))){
                        /* Expand $@ / $* into array elements */
                        fprintf(out,"    { int __ai=0; for(int __j=0;__j<__sh_argc&&__ai<255;__j++){ __arr_%s[__ai++]=strdup(__sh_args[__j]?__sh_args[__j]:\"\"); } __arr_%s[__ai]=NULL; }\n",cn,cn);
                        /* skip past the whole ("$@") or ("${@}") */
                        continue;
                    }
                    while(*p && *p!=')'){
                        while(*p==' '||*p=='\t') p++;
                        if(*p==')') break;
                        char elem[256]; int ei=0;
                        if(*p=='"'){ p++; while(*p&&*p!='"'&&ei<(int)sizeof(elem)-1){
                            if(*p=='\\' && *(p+1)){
                                char next=*(p+1);
                                if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n') p++;
                            }
                            elem[ei++]=*p++;
                        } if(*p=='"')p++; }
                        else if(*p=='\''){ p++; while(*p&&*p!='\''&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='\'')p++; }
                        else { while(*p&&*p!=' '&&*p!='\t'&&*p!=')'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; }
                        elem[ei]=0;
                        { char *esc=c_escape_literal(elem);
                        fprintf(out,"    __arr_%s[%d]=\"%s\";\n",cn,idx,esc);
                        free(esc); }
                        idx++;
                    }
                } else {
                    add_var(n->argv[i],V_STR);
                    /* handle quoted strings with variable expansion */
                    const char *val=eq+1;
                    if(val[0]=='"' || val[0]=='\''){
                        /* strip quotes */
                        char inner[1024]; int il=0;
                        const char *vp=val+1;
                        while(*vp && *vp!=val[0] && il<(int)sizeof(inner)-1){
                            if(val[0]=='"' && *vp=='\\' && *(vp+1)){
                                char next=*(vp+1);
                                if(next=='$'||next=='"'||next=='\\'||next=='`'||next=='\n') vp++;
                            }
                            inner[il++]=*vp++;
                        }
                        inner[il]=0;
                        if(strchr(inner,'$')){
                            /* expand variables */
                            ExpandResult er; expand_string(inner,&er);
                            fprintf(out,"    snprintf(%s,sizeof(%s),\"%s\"",cn,cn,er.fmt);
                            for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                            fprintf(out,");\n");
                            expand_free(&er);
                        } else {
                            char *esc=c_escape_literal(inner);
                            fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",cn,esc,cn);
                            free(esc);
                        }
                    } else if(strchr(val,'$')){
                        ExpandResult er; expand_string(val,&er);
                        fprintf(out,"    snprintf(%s,sizeof(%s),\"%s\"",cn,cn,er.fmt);
                        for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                        fprintf(out,");\n");
                        expand_free(&er);
                    } else {
                        char *esc=c_escape_literal(val);
                        fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",cn,esc,cn);
                        free(esc);
                    }
                }
                *eq='=';
            } else {
                /* just declaration, no value */
            }
        }
        break;
    }

    case NODE_CMD:
    case NODE_BACKGROUND:{
        int is_bg=(n->type==NODE_BACKGROUND);
        /* collect argv without redirects */
        char *argv[256]; int ac=0;
        Redir *rl=n->redirs;
        Redir *rl_tail=NULL;
        for(Redir *t=rl;t;t=t->next) rl_tail=t;  /* find list tail */
        int skip=0;
        for(int i=0;i<n->argc;i++){
            if(skip){skip=0;continue;}
            /* inline redirects like >file, <file, >>file, 2>file, &>file, 2>&1, >&N */
            const char *t=n->argv[i];
            int prefix_fd=-1;
            /* detect N> or N>& where N is a single digit (e.g. 2>, 2>&1) */
            if(t[0] && isdigit((unsigned char)t[0]) && t[1]==0 && i+1<n->argc){
                const char *nx=n->argv[i+1];
                if(nx[0]=='>'||nx[0]=='<'){ prefix_fd=atoi(t); t=nx; i++; skip=0; }
            }
            /* skip process substitution <(cmd) and >(cmd) — they're arguments */
            if((t[0]=='<'||t[0]=='>')&&t[1]=='('){
                argv[ac++]=n->argv[i]; continue;
            }
            if(t[0]=='>'||t[0]=='<'||(t[0]=='2'&&t[1]=='>')||(t[0]=='&'&&t[1]=='>')||!strcmp(t,">>")||!strcmp(t,"<<<")||!strcmp(t,">&")||!strcmp(t,"<<")){
                int fd=1,append=0,dup_fd=-1,is_hs=0,is_hd=0; const char *file=NULL;
                const char *op=t;
                if(op[0]=='&'&&op[1]=='>'){ fd=1; file=n->argv[i+1]; skip=1; }
                else if(!strcmp(op,">&")){ fd=1; const char *nxt=n->argv[i+1]; if(nxt&&isdigit((unsigned char)nxt[0])){dup_fd=atoi(nxt);} else {file=nxt;} skip=1; }
                else if(op[0]=='2'&&op[1]=='>'){ fd=2; if(op[2]=='>'){append=1;file=(op[3]?op+3:n->argv[i+1]); if(!op[3])skip=1;} else {file=(op[2]?op+2:n->argv[i+1]); if(!op[2])skip=1;} }
                else if(op[0]=='>'&&op[1]=='>'){ fd=1; append=1; file=(op[2]?op+2:n->argv[i+1]); if(!op[2])skip=1; }
                else if(op[0]=='>'){ fd=1; file=(op[1]?op+1:n->argv[i+1]); if(!op[1])skip=1; }
                else if(!strcmp(op,"<<<")){ fd=0; is_hs=1; file=n->argv[i+1]; skip=1; }
                else if(!strcmp(op,"<<")){ fd=0; is_hd=1; skip=0; /* heredoc text from table */ }
                else if(op[0]=='<'){ fd=0; file=(op[1]?op+1:n->argv[i+1]); if(!op[1])skip=1; }
                /* prefix_fd (e.g. 2>) overrides the default fd */
                if(prefix_fd>=0) fd=prefix_fd;
                /* handle >&N (dup fd) */
                if(file && file[0]=='&' && isdigit((unsigned char)file[1])){
                    dup_fd=atoi(file+1);
                    file=NULL;
                }
                Redir *r;
                if(is_hs) r=new_redir(fd,NULL,0,-1,0,1,file);
                else if(is_hd){
                    int hd_expand=1;
                    const char *hd_text=heredoc_consume(&hd_expand);
                    r=new_redir(fd,NULL,0,-1,1,0,hd_text);
                    r->hd_expand=hd_expand;
                }
                else r=new_redir(fd,file,append,dup_fd,0,0,NULL);
                r->next=NULL;
                if(rl_tail) rl_tail->next=r;
                else rl=r;
                rl_tail=r;
                continue;
            }
            argv[ac++]=n->argv[i];
        }
        n->redirs=rl;
        if(ac==0) break;

        if(is_bg) fprintf(out,"    { pid_t __bg=fork(); if(__bg==0){\n");
        fprintf(out,"    {\n");
        /* save fds */
        emit_redirs_save(out,n->redirs,n->lineno);
        /* apply redirects */
        emit_redirs_apply(out,n->redirs,n->lineno);
        /* dispatch */
        emit_command(out,argv,ac,n->lineno);
        /* restore fds */
        emit_redirs_restore(out,n->redirs,n->lineno);
        fprintf(out,"    }\n");
        if(is_bg){
            fprintf(out,"    exit(0); } else { __sh_last_bg_pid=(int)__bg; }\n");
            fprintf(out,"    }\n");
        }
        /* BUG-15 fix: set -e — check __exit_status after each command */
        if(!is_bg)
            fprintf(out,"    if(__sh_set_e && __exit_status!=0) exit(__exit_status);\n");
        break;
    }

    case NODE_IF:{
        char *c=translate_cond(n->cond);
        fprintf(out,"    if(%s){\n",c);
        emit_node(out,n->then_blk);
        for(int i=0;i<n->elif_count;i++){
            char *ec=translate_cond(n->elif_conds[i]->cond);
            fprintf(out,"    } else if(%s){\n",ec);
            emit_node(out,n->elif_blks[i]);
        }
        if(n->else_blk){ fprintf(out,"    } else {\n"); emit_node(out,n->else_blk); }
        fprintf(out,"    }\n"); break;
    }

    case NODE_FOR:{
        if(n->for_c_style){
            /* for ((init; cond; update)) — translate_arith handles V_STR vars */
            fprintf(out,"    for(%s; %s; %s){\n",
                    n->for_init?n->for_init:"",
                    n->for_cond?n->for_cond:"",
                    n->for_update?n->for_update:"");
            emit_node(out,n->body);
            fprintf(out,"    }\n");
        } else {
            const char *vn=safe_cname(n->for_var);
            /* Check if the list is a single unquoted $var or $(cmd) — if so, word-split */
            int do_split_var = (n->for_len==1 && n->for_list[0][0]=='$'
                            && n->for_list[0][1]!='{' && n->for_list[0][1]!='('
                            && get_var_kind(n->for_list[0]+1)==V_STR);
            int do_split_cmd = (n->for_len==1 && n->for_list[0][0]=='$'
                            && n->for_list[0][1]=='(');
            /* Check for ${arr[@]} or ${arr[*]} — array iteration */
            int do_array_iter = 0;
            char arr_name[128];
            if(n->for_len==1){
                const char *item=n->for_list[0];
                /* check for "${arr[@]}" (quoted) or ${arr[@]} (unquoted) */
                const char *p=item;
                if(*p=='"') p++; /* skip opening quote */
                if(*p=='$' && *(p+1)=='{'){
                    p+=2;
                    int aj=0;
                    while(*p && *p!='[' && *p!='}' && aj<(int)sizeof(arr_name)-1) arr_name[aj++]=*p++;
                    arr_name[aj]=0;
                    if(*p=='[' && (!strcmp(p,"[@]}")||!strcmp(p,"[@]\"}")||!strcmp(p,"[*]}")||!strcmp(p,"[*]\"}"))){
                        if(get_var_kind(arr_name)==V_ARRAY){
                            do_array_iter=1;
                        }
                    }
                }
            }
            if(do_array_iter){
                /* iterate over array elements directly */
                fprintf(out,"    {\n");
                fprintf(out,"    for(int __ai=0;__arr_%s[__ai];__ai++){\n",safe_cname(arr_name));
                VarKind vk=get_var_kind(n->for_var);
                if(vk==V_INT)
                    fprintf(out,"    %s=atoi(__arr_%s[__ai]);\n",vn,safe_cname(arr_name));
                else
                    fprintf(out,"    strncpy(%s,__arr_%s[__ai],sizeof(%s)-1);\n",vn,safe_cname(arr_name),vn);
                emit_node(out,n->body);
                fprintf(out,"    }\n");
                fprintf(out,"    }\n");
            } else if(do_split_var){
                const char *listexpr = n->for_list[0]+1;
                /* $@ / $* → __sh_join_args(__sh_argc, __sh_args) */
                int is_args = (listexpr[0]=='@' || listexpr[0]=='*');
                const char *vname = is_args ? "__sh_join_args(__sh_argc,__sh_args)" : safe_cname(listexpr);
                fprintf(out,"    {\n");
                fprintf(out,"    char __sbuf[65536]; strncpy(__sbuf,%s,sizeof(__sbuf)-1); __sbuf[sizeof(__sbuf)-1]=0;\n",vname);
                fprintf(out,"    char *__sv=__sbuf;\n");
                /* BUG-12 fix: use IFS for word splitting */
                fprintf(out,"    const char *__ifs=getenv(\"IFS\"); if(!__ifs)__ifs=\" \\t\\n\";\n");
                fprintf(out,"    while(*__sv){\n");
                fprintf(out,"        while(*__sv&&strchr(__ifs,*__sv))__sv++;\n");
                fprintf(out,"        if(!*__sv)break;\n");
                fprintf(out,"        char *__se=__sv;\n");
                fprintf(out,"        while(*__se&&!strchr(__ifs,*__se))__se++;\n");
                fprintf(out,"        char __sc=*__se; *__se=0;\n");
                VarKind vk=get_var_kind(n->for_var);
                if(vk==V_INT)
                    fprintf(out,"        %s=atoi(__sv);\n",vn);
                else
                    fprintf(out,"        strncpy(%s,__sv,sizeof(%s)-1); %s[sizeof(%s)-1]=0;\n",vn,vn,vn,vn);
                emit_node(out,n->body);
                fprintf(out,"        *__se=__sc; __sv=( *__se?__se:__se);\n");
                fprintf(out,"        __sv=__se; if(*__sv)__sv++;\n");
                fprintf(out,"    }\n");
                fprintf(out,"    }\n");
            } else if(do_split_cmd){
                /* $(cmd) — execute, word-split output, iterate */
                char *w=emit_word(out,n->for_list[0]);
                fprintf(out,"    {\n");
                fprintf(out,"    char __sbuf[65536]; strncpy(__sbuf,%s,sizeof(__sbuf)-1); __sbuf[sizeof(__sbuf)-1]=0;\n",w);
                free(w);
                fprintf(out,"    char *__sv=__sbuf;\n");
                /* Use IFS for word splitting (same as do_split_var path) */
                fprintf(out,"    const char *__ifs=getenv(\"IFS\"); if(!__ifs)__ifs=\" \\t\\n\";\n");
                fprintf(out,"    while(*__sv){\n");
                fprintf(out,"        while(*__sv&&strchr(__ifs,*__sv))__sv++;\n");
                fprintf(out,"        if(!*__sv)break;\n");
                fprintf(out,"        char *__se=__sv;\n");
                fprintf(out,"        while(*__se&&!strchr(__ifs,*__se))__se++;\n");
                fprintf(out,"        char __sc=*__se; *__se=0;\n");
                VarKind vk=get_var_kind(n->for_var);
                if(vk==V_INT)
                    fprintf(out,"        %s=atoi(__sv);\n",vn);
                else
                    fprintf(out,"        strncpy(%s,__sv,sizeof(%s)-1); %s[sizeof(%s)-1]=0;\n",vn,vn,vn,vn);
                emit_node(out,n->body);
                fprintf(out,"        *__se=__sc; __sv=__se; if(*__sv)__sv++;\n");
                fprintf(out,"    }\n");
                fprintf(out,"    }\n");
            } else {
            /* For-loop with literal list: use stack-allocated array, no malloc/strdup */
            fprintf(out,"    {\n");
            /* Check if any list item contains glob chars */
            int has_glob=0;
            for(int i=0;i<n->for_len;i++){
                for(const char *g=n->for_list[i];*g;g++){
                    if(*g=='*'||*g=='?'||*g=='['){ has_glob=1; break; }
                }
                if(has_glob) break;
            }
            if(has_glob && n->for_len<=16){
                /* Use runtime glob() for each item */
                fprintf(out,"    const char *__fl%d[256]; int __nfl%d=0;\n",n->lineno,n->lineno);
                for(int i=0;i<n->for_len;i++){
                    char *esc=c_escape_literal(n->for_list[i]);
                    fprintf(out,"    { glob_t __g; if(glob(\"%s\",0,NULL,&__g)==0){ for(unsigned __gi=0;__gi<__g.gl_pathc&&__nfl%d<255;__gi++) __fl%d[__nfl%d++]=strdup(__g.gl_pathv[__gi]); } globfree(&__g); }\n",esc,n->lineno,n->lineno,n->lineno);
                    free(esc);
                }
                fprintf(out,"    __fl%d[__nfl%d]=NULL;\n",n->lineno,n->lineno);
            } else {
            fprintf(out,"    const char *__fl%d[] = {",n->lineno);
            for(int i=0;i<n->for_len;i++){
                char *w=emit_word(out,n->for_list[i]);
                fprintf(out,"%s%s",(i>0?", ":""),w);
                free(w);
            }
            if(n->for_len==0)
                fprintf(out,"NULL};\n");
            else
                fprintf(out,", NULL};\n");
            }
            fprintf(out,"    for (int __fi%d = 0; __fl%d[__fi%d]; __fi%d++) {\n",
                    n->lineno,n->lineno,n->lineno,n->lineno);
            VarKind vk=get_var_kind(n->for_var);
            if(vk==V_INT)
                fprintf(out,"        %s = atoi(__fl%d[__fi%d]);\n",vn,n->lineno,n->lineno);
            else
                fprintf(out,"        strncpy(%s, __fl%d[__fi%d], sizeof(%s) - 1);\n",
                        vn,n->lineno,n->lineno,vn);
            emit_node(out,n->body);
            fprintf(out,"    }\n");
            fprintf(out,"    }\n");
            }
        }
        break;
    }

    case NODE_WHILE:{
        /* Handle redirects (e.g., done < file) */
        if(n->redirs){
            fprintf(out,"    {\n");
            emit_redirs_save(out,n->redirs,n->lineno);
            emit_redirs_apply(out,n->redirs,n->lineno);
        }
        /* Check for pending pipe command (cmd | while ...) */
        if(pending_pipe_cmd){
            fprintf(out,"    { int __ppfd[2]; if(pipe(__ppfd)<0){perror(\"pipe\");exit(1);}\n");
            fprintf(out,"    pid_t __ppid=fork();\n");
            fprintf(out,"    if(__ppid==0){ close(__ppfd[0]); dup2(__ppfd[1],1); close(__ppfd[1]);\n");
            emit_node(out,pending_pipe_cmd);
            fprintf(out,"    fflush(stdout); _exit(0); }\n");
            fprintf(out,"    close(__ppfd[1]); int __psi=dup(0); dup2(__ppfd[0],0); close(__ppfd[0]); clearerr(stdin);\n");
            pending_pipe_cmd=NULL;
            pipe_restore_needed=1;
            /* fall through to normal while handling, but mark for restore */
        }
        /* Check if condition starts with IFS= assignment before read */
        const char *cond_str = n->while_cond ? n->while_cond : "";
        const char *read_start = NULL;
        char ifs_value[256] = "";
        if(!strncmp(cond_str,"IFS=",4)){
            /* Extract IFS value */
            const char *vp=cond_str+4;
            if(*vp=='"'){
                vp++; int il=0;
                while(*vp&&*vp!='"'&&il<255) ifs_value[il++]=*vp++;
                if(*vp=='"') vp++; /* skip closing quote */
            } else {
                int il=0;
                while(*vp&&*vp!=' '&&il<255) ifs_value[il++]=*vp++;
            }
            /* Find 'read' after IFS= */
            while(*vp==' ') vp++;
            if(!strncmp(vp,"read",4)) read_start=vp;
        } else if(!strncmp(cond_str,"read",4) && (cond_str[4]==' '||cond_str[4]==0)){
            read_start=cond_str;
        }
        /* Check if condition is a read command (while read -r line) */
        if(read_start){
            /* Parse read variables from condition */
            char vars[8][128]; int nv=0;
            const char *p=read_start+4;
            while(*p && nv<8){
                while(*p==' '||*p=='\t') p++;
                if(!*p) break;
                if(*p=='-'){ p++; while(*p&&*p!=' ')p++; continue; }
                int vl=0;
                while(*p && *p!=' ' && *p!='\t' && vl<127) vars[nv][vl++]=*p++;
                vars[nv][vl]=0;
                if(vl>0){ add_var(vars[nv],V_STR); nv++; }
            }
            fprintf(out,"    { char __rb[65536];\n");
            if(ifs_value[0]){
                /* Set IFS for this loop */
                char *esc=c_escape_literal(ifs_value);
                fprintf(out,"    const char *__ifs=\"%s\";\n",esc);
                free(esc);
            }
            if(n->while_negate){
                fprintf(out,"    while(!fgets(__rb,sizeof(__rb),stdin)){\n");
            } else {
                fprintf(out,"    while(fgets(__rb,sizeof(__rb),stdin)){\n");
            }
            fprintf(out,"    int __rl=(int)strlen(__rb); if(__rl>0&&__rb[__rl-1]=='\\n')__rb[--__rl]=0;\n");
            if(nv==1){
                fprintf(out,"    strncpy(%s,__rb,sizeof(%s)-1);\n",safe_cname(vars[0]),safe_cname(vars[0]));
            } else if(nv>1){
                if(ifs_value[0]){
                    fprintf(out,"    char *__sp=__rb; int __vi=0; char *__tok=strtok(__sp,__ifs);\n");
                    fprintf(out,"    while(__tok&&__vi<%d){ switch(__vi){\n",nv);
                    for(int i=0;i<nv;i++)
                        fprintf(out,"    case %d: strncpy(%s,__tok,sizeof(%s)-1); break;\n",i,safe_cname(vars[i]),safe_cname(vars[i]));
                    fprintf(out,"    } __vi++; __tok=strtok(NULL,__ifs); }\n");
                } else {
                    fprintf(out,"    char *__sp=__rb; int __vi=0; char *__tok=strtok(__sp,\" \\t\");\n");
                    fprintf(out,"    while(__tok&&__vi<%d){ switch(__vi){\n",nv);
                    for(int i=0;i<nv;i++)
                        fprintf(out,"    case %d: strncpy(%s,__tok,sizeof(%s)-1); break;\n",i,safe_cname(vars[i]),safe_cname(vars[i]));
                    fprintf(out,"    } __vi++; __tok=strtok(NULL,\" \\t\"); }\n");
                }
            }
            emit_node(out,n->while_body);
            fprintf(out,"    }\n    }\n");
        } else {
            char *c=translate_cond(n->while_cond);
            if(n->while_negate) fprintf(out,"    while(!(%s)){\n",c);
            else                fprintf(out,"    while(%s){\n",c);
            emit_node(out,n->while_body);
            fprintf(out,"    }\n");
        }
        if(pipe_restore_needed){
            fprintf(out,"    fflush(stdout); dup2(__psi,0); close(__psi); clearerr(stdin); waitpid(__ppid,NULL,0);\n");
            fprintf(out,"    }\n");
            pipe_restore_needed=0;
        }
        /* Restore redirects */
        if(n->redirs){
            emit_redirs_restore(out,n->redirs,n->lineno);
            fprintf(out,"    }\n");
        }
        break;
    }

    /* SELECT loop: print menu, read input, execute body */
    case NODE_SELECT:{
        const char *vn=safe_cname(n->for_var);
        fprintf(out,"    {\n");
        fprintf(out,"    for(;;){\n");
        fprintf(out,"    int __si=1;\n");
        for(int i=0;i<n->for_len;i++){
            char *esc=c_escape_literal(n->for_list[i]);
            fprintf(out,"    printf(\"%%d) %s\\n\",__si++);\n",esc?esc:"");
            free(esc);
        }
        fprintf(out,"    printf(\"#? \"); fflush(stdout);\n");
        fprintf(out,"    char __sl[65536]; if(!fgets(__sl,sizeof(__sl),stdin)) break;\n");
        fprintf(out,"    __sl[strcspn(__sl,\"\\r\\n\")]=0;\n");
        fprintf(out,"    int __sn=atoi(__sl);\n");
        fprintf(out,"    if(__sn<1||__sn>%d) continue;\n",n->for_len);
        fprintf(out,"    setenv(\"REPLY\",__sl,1);\n");
        fprintf(out,"    switch(__sn){\n");
        for(int i=0;i<n->for_len;i++){
            char *esc=c_escape_literal(n->for_list[i]);
            fprintf(out,"    case %d: strncpy(%s,\"%s\",sizeof(%s)-1); break;\n",
                    i+1, vn, esc?esc:"", vn);
            free(esc);
        }
        fprintf(out,"    default: break;\n");
        fprintf(out,"    }\n");
        emit_node(out,n->body);
        fprintf(out,"    }\n");
        fprintf(out,"    }\n");
        break;
    }

    case NODE_UNTIL: break;  /* handled as while_negate */
    case NODE_ARITH: break;  /* ((expr)) as statement */
    case NODE_FUNC: break;

    case NODE_BREAK:    fprintf(out,"    break;\n"); break;
    case NODE_CONTINUE: fprintf(out,"    continue;\n"); break;
    case NODE_RETURN:
        if(n->exit_str){
            fprintf(out,"    { __exit_status=atoi(%s); return; }\n",n->exit_str);
        } else if(n->exit_code>=0){
            fprintf(out,"    { __exit_status=%d; return; }\n",n->exit_code);
        } else {
            fprintf(out,"    return;\n");
        }
        break;
    case NODE_EXIT:
        if(n->exit_str){
            fprintf(out,"    exit(atoi(%s));\n",n->exit_str);
        } else {
            fprintf(out,"    exit(%d);\n",n->exit_code>=0?n->exit_code:0);
        }
        break;

    case NODE_PIPE: emit_pipe(out,n); break;

    case NODE_AND:{
        emit_node(out,n->left);
        fprintf(out,"    if(__exit_status==0){\n");
        emit_node(out,n->right);
        fprintf(out,"    }\n");
        break;
    }
    case NODE_OR:{
        emit_node(out,n->left);
        fprintf(out,"    if(__exit_status!=0){\n");
        emit_node(out,n->right);
        fprintf(out,"    }\n");
        break;
    }

    case NODE_SUBSHELL:{
        fprintf(out,"    { pid_t __sp=fork(); if(__sp==0){\n");
        emit_node(out,n->left);
        fprintf(out,"    _exit(__exit_status); } else { int __st; waitpid(__sp,&__st,0); __exit_status=WIFEXITED(__st)?WEXITSTATUS(__st):1; } }\n");
        break;
    }

    case NODE_GROUP:{
        fprintf(out,"    {\n");
        emit_node(out,n->left);
        fprintf(out,"    }\n");
        break;
    }

    case NODE_HEREDOC:
        if(n->heredoc_text){
            fprintf(out,"    fputs(\"");
            for(const char *c=n->heredoc_text;*c;c++){
                if(*c=='\\'||*c=='"') fprintf(out,"\\");
                if(*c=='\n') fprintf(out,"\\n");
                else if(*c=='\t') fprintf(out,"\\t");
                else fprintf(out,"%c",*c);
            }
            fprintf(out,"\",stdout);\n");
        }
        break;

    case NODE_CASE:{
        char *cv=n->case_var?n->case_var:(char*)"\"\"";
        /* cv is already a C expression from translate_expr, don't use safe_cname */
        for(int ci=0;ci<n->case_count;ci++){
            const char *pat=n->case_pats[ci];
            if(ci==0) fprintf(out,"    ");
            else fprintf(out,"    } else ");
            if(!strcmp(pat,"*")) fprintf(out,"if(1){\n");
            else {
                fprintf(out,"if(");
                /* Quote-aware split on | — don't split | inside quotes */
                char patcopy[256]; strncpy(patcopy,pat,255); patcopy[255]=0;
                int first=1;
                const char *ps=patcopy;
                while(*ps){
                    /* Skip leading whitespace */
                    while(*ps==' ') ps++;
                    if(!*ps) break;
                    /* Read one pattern (until | outside quotes) */
                    char tok[256]; int tl=0;
                    int in_dq=0,in_sq=0;
                    while(*ps && tl<255){
                        if(*ps=='"'&&!in_sq) in_dq=!in_dq;
                        else if(*ps=='\''&&!in_dq) in_sq=!in_sq;
                        else if(!in_dq&&!in_sq && *ps=='|') break;
                        tok[tl++]=*ps++;
                    }
                    tok[tl]=0;
                    if(*ps=='|') ps++;
                    /* Strip surrounding quotes for fnmatch/strcmp */
                    char clean[256]; int cl=0;
                    for(int qi=0;qi<tl&&cl<255;qi++){
                        if(tok[qi]=='"'||tok[qi]=='\'') continue; /* skip quotes */
                        if(tok[qi]=='\\'&&qi+1<tl) clean[cl++]=tok[++qi];
                        else clean[cl++]=tok[qi];
                    }
                    clean[cl]=0;
                    if(!first) fprintf(out,"||");
                    first=0;
                    /* check if pattern has wildcards for fnmatch */
                    int has_glob = (strchr(clean,'*')||strchr(clean,'?')||strchr(clean,'['));
                    if(has_glob){
                        fprintf(out,"(fnmatch(\"%s\",%s,0)==0)",clean,cv);
                    } else {
                        fprintf(out,"(strcmp(%s, \"%s\") == 0)",cv,clean);
                    }
                }
                fprintf(out,"){\n");
            }
            emit_node(out,n->case_bodies[ci]);
        }
        if(n->case_default){
            if(n->case_count>0) fprintf(out,"    } else {\n");
            else fprintf(out,"    if(1){\n");
            emit_node(out,n->case_default);
        }
        if(n->case_count>0||n->case_default) fprintf(out,"    }\n");
        break;
    }

    case NODE_TRAP:
        {
            const char *action=n->trap_action?n->trap_action:"";
            int sig=n->trap_sig;
            /* Check if action is a bare user function name (no quotes, spaces, or special chars) */
            int is_simple_name=1;
            for(const char *p=action;*p;p++){
                if(!isalnum((unsigned char)*p)&&*p!='_'){ is_simple_name=0; break; }
            }
            if(is_simple_name && is_user_func(action) && sig==0){
                /* EXIT trap with user function: register wrapper */
                fprintf(out,"    atexit(__b_trap_exit_handler); __sh_trap_actions[0]=\"%s\"; __sh_trap_is_func[0]=1; __sh_trap_func[0]=(void(*)(int,char**))%s;\n",action,safe_cname(action));
            } else if(is_simple_name && is_user_func(action) && sig>0){
                /* Signal trap with user function */
                fprintf(out,"    __sh_trap_actions[%d]=\"%s\"; __sh_trap_is_func[%d]=1; __sh_trap_func[%d]=(void(*)(int,char**))%s; signal(%d,__b_trap_sighandler);\n",sig,action,sig,sig,safe_cname(action),sig);
            } else {
                fprintf(out,"    __b_trap(\"%s\",%d);\n",action,sig);
            }
        }
        break;

    default: break;
    }
    emit_node(out,n->next);
}

static void emit_block(FILE *out, Node *n) __attribute__((unused));
static void emit_block(FILE *out, Node *n){
    emit_node(out,n);
}

/* Scan a node tree for local variable declarations */
void scan_locals(Node *n, char locals[][128], int *nloc, int max){
    if(!n || *nloc>=max) return;
    if(n->type==NODE_LOCAL){
        for(int i=0;i<n->argc;i++){
            char *eq=strchr(n->argv[i],'=');
            if(eq) *eq=0;
            if(n->argv[i][0]){
                int found=0;
                for(int j=0;j<*nloc;j++) if(strcmp(locals[j],n->argv[i])==0){found=1;break;}
                if(!found && *nloc<max){
                    strncpy(locals[*nloc],n->argv[i],127);
                    locals[*nloc][127]=0;
                    (*nloc)++;
                }
            }
            if(eq) *eq='=';
        }
    }
    /* recurse into sub-nodes */
    if(n->next) scan_locals(n->next,locals,nloc,max);
    if(n->then_blk) scan_locals(n->then_blk,locals,nloc,max);
    if(n->else_blk) scan_locals(n->else_blk,locals,nloc,max);
    if(n->body) scan_locals(n->body,locals,nloc,max);
    if(n->while_body) scan_locals(n->while_body,locals,nloc,max);
    if(n->func_body) scan_locals(n->func_body,locals,nloc,max);
}

void emit_functions(FILE *out, Node *n){
    if(!n) return;
    if(n->type==NODE_FUNC){
        fprintf(out,"static void %s(int __sh_argc, char **__sh_args){\n",safe_cname(n->fname));
        /* Only declare positional parameters that are actually used in the body */
        int max_arg=0;
        /* Simple heuristic: scan function body for $1..$9 references */
        if(n->func_body){
            /* For simplicity, always declare args 1-3 (most common) */
            max_arg=3;
        }
        for(int i=1;i<=max_arg;i++)
            fprintf(out,"    char __sh_arg%d[65536]=\"\"; if(__sh_argc>=%d)strncpy(__sh_arg%d,__sh_args[%d-1],65535);\n",
                    i,i,i,i);
        /* Scan for local variables and save them */
        char locals[32][128]; int nloc=0;
        scan_locals(n->func_body,locals,&nloc,32);
        for(int i=0;i<nloc;i++){
            VarKind vk=get_var_kind(locals[i]);
            const char *cn=safe_cname(locals[i]);
            if(vk==V_INT){
                fprintf(out,"    int __save_%s=%s;\n",cn,cn);
            } else if(vk==V_ARRAY){
                /* skip array save/restore for now */
            } else {
                fprintf(out,"    char __save_%s[1024]; strncpy(__save_%s,%s,sizeof(__save_%s)-1);\n",cn,cn,cn,cn);
            }
        }
        emit_node(out,n->func_body);
        /* Restore local variables */
        for(int i=0;i<nloc;i++){
            VarKind vk=get_var_kind(locals[i]);
            const char *cn=safe_cname(locals[i]);
            if(vk==V_INT){
                fprintf(out,"    %s=__save_%s;\n",cn,cn);
            } else if(vk==V_ARRAY){
                /* skip */
            } else {
                fprintf(out,"    strncpy(%s,__save_%s,sizeof(%s)-1);\n",cn,cn,cn);
            }
        }
        fprintf(out,"}\n\n");
    }
    emit_functions(out,n->next);
}

