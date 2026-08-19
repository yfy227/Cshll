/* ================================================================== */
/* L9  Parser                                                         */
/* ================================================================== */

/* BlkFrame, BlkKind, blk_stack, blk_top, parse_root, parse_insert
 * are declared in s2c_parse.h. Definitions here: */
/* Dynamic linked-list stack — no depth limit */
BlkFrame *blk_top=NULL;
Node *parse_root=NULL;
Node **parse_insert=NULL;
/* pending_pipe_cmd and pipe_restore_needed declared in L8 emitter section */

/* blk_depth: track nesting depth for diagnostics (no functional limit) */
static int blk_depth=0;

static Node **chain_tail(Node **hp){
    if(!hp) return NULL;
    Node *n=*hp;
    if(!n) return hp;
    while(n->next) n=n->next;
    return &n->next;
}

static void parser_append(Node *n){
    if(!parse_insert) parse_root=n;
    else *parse_insert=n;
    parse_insert=&n->next;
}

static void parser_push(BlkFrame fr){
    BlkFrame *p=(BlkFrame*)malloc(sizeof(BlkFrame));
    if(!p){fprintf(stderr,"parser_push: out of memory\n");return;}
    *p=fr;
    p->next=blk_top;
    blk_top=p;
    blk_depth++;
}

static void parser_pop(void){
    if(!blk_top) return;
    BlkFrame *p=blk_top;
    Node **pi=p->parent_insert;
    blk_top=p->next;
    blk_depth--;
    free(p);
    if(pi) parse_insert=chain_tail(pi);
    else{
        if(parse_root){Node *sc=parse_root;while(sc->next)sc=sc->next;parse_insert=&sc->next;}
        else parse_insert=NULL;
    }
}

void strip_comment(char *line){
    int dq=0,sq=0,brace=0;
    for(int i=0;line[i];i++){
        if(line[i]=='"'&&!sq) dq=!dq;
        if(line[i]=='\''&&!dq) sq=!sq;
        if(!dq&&!sq){
            if(line[i]=='{') brace++;
            else if(line[i]=='}'&&brace>0) brace--;
            /* # is a comment only at word boundary (start of line or
             * preceded by whitespace) and NOT inside ${...} */
            if(line[i]=='#' && brace==0){
                if(i==0 || isspace((unsigned char)line[i-1])){
                    line[i]=0; return;
                }
            }
        }
    }
}

int is_assignment(const char *t){
    /* Check for = += -= *= /= %= */
    const char *eq=NULL;
    const char *p=t;
    while(*p){
        if(*p=='=' && p>t){
            /* check for += -= *= /= %= */
            if(p>t && (p[-1]=='+'||p[-1]=='-'||p[-1]=='*'||p[-1]=='/'||p[-1]=='%')){
                eq=p-1; break;
            }
            eq=p; break;
        }
        if(*p=='='){ eq=p; break; }
        p++;
    }
    if(!eq||eq==t) return 0;
    for(const char *q=t;q<eq;q++)
        if(!isalnum((unsigned char)*q)&&*q!='_') return 0;
    return 1;
}

/* Check if token is an append assignment (+=, -=, etc.) */
int is_append_assign(const char *t) __attribute__((unused));
int is_append_assign(const char *t){
    const char *p=strstr(t,"+=");
    if(p && p>t){
        for(const char *q=t;q<p;q++)
            if(!isalnum((unsigned char)*q)&&*q!='_') return 0;
        return 1;
    }
    return 0;
}

/* Check if token is an array element assignment: arr[key]=value or arr[key]="value" */
int is_array_assignment(const char *t){
    /* pattern: name[...]=... */
    const char *lb=strchr(t,'[');
    if(!lb || lb==t) return 0;
    /* name part must be alphanumeric/underscore */
    for(const char *q=t;q<lb;q++)
        if(!isalnum((unsigned char)*q)&&*q!='_') return 0;
    /* find matching ] */
    const char *rb=strchr(lb,']');
    if(!rb) return 0;
    /* after ] must be = or += */
    if(*rb!=']') return 0;
    const char *eq=rb+1;
    if(*eq=='+'&&*(eq+1)=='=') eq++;
    if(*eq!='=') return 0;
    return 1;
}

/* Extract array name from arr[key]=value */
void extract_array_assign(const char *t, char *name, int name_sz, char *key, int key_sz, char *val, int val_sz, int *is_append){
    const char *lb=strchr(t,'[');
    const char *rb=strchr(lb,']');
    int nl=(int)(lb-t);
    if(nl>=name_sz) nl=name_sz-1;
    memcpy(name,t,nl); name[nl]=0;
    int kl=(int)(rb-lb-1);
    if(kl>=key_sz) kl=key_sz-1;
    memcpy(key,lb+1,kl); key[kl]=0;
    /* Strip surrounding quotes from key */
    { int kl2=(int)strlen(key);
      if(kl2>=2 && ((key[0]=='"'&&key[kl2-1]=='"') || (key[0]=='\''&&key[kl2-1]=='\''))){
        memmove(key,key+1,kl2-2); key[kl2-2]=0;
      }
    }
    const char *eq=rb+1;
    *is_append=0;
    if(*eq=='+'){ *is_append=1; eq++; }
    eq++; /* skip = */
    strncpy(val,eq,val_sz-1); val[val_sz-1]=0;
}

int find_op(char **toks,int n,const char *op){
    int d=0;
    for(int i=0;i<n;i++){
        if(!strcmp(toks[i],"[")||!strcmp(toks[i],"[[")||!strcmp(toks[i],"(")) d++;
        if(!strcmp(toks[i],"]")||!strcmp(toks[i],"]]")||!strcmp(toks[i],")")) d--;
        if(!d && !strcmp(toks[i],op)) return i;
    }
    return -1;
}

Node *make_cmd(char **toks,int n,int ln){
    Node *nd=new_node(NODE_CMD,ln);
    nd->argv=malloc((n+1)*sizeof(char*)); nd->argc=n;
    for(int i=0;i<n;i++) nd->argv[i]=xstrdup(toks[i]);
    nd->argv[n]=NULL;
    /* If this is a 'read' command, register its target variables as strings */
    if(n>=2 && !strcmp(toks[0],"read")){
        int has_a=0; /* -a flag: read into array */
        for(int i=1;i<n;i++){
            if(toks[i][0]=='-'){
                /* -p PROMPT: skip flag AND its argument */
                if(!strcmp(toks[i],"-p") && i+1<n){
                    i++; /* skip prompt string */
                    continue;
                }
                for(const char *fc=toks[i];*fc;fc++) if(*fc=='a') has_a=1;
                continue;  /* skip flags like -r, -p */
            }
            if(!strcmp(toks[i],"<<<")||!strcmp(toks[i],"<<")) break;  /* stop at here-string/heredoc */
            if(!strcmp(toks[i],"<")) break;  /* stop at redirect */
            add_var(toks[i], has_a ? V_ARRAY : V_STR);
        }
    }
    /* If this is an 'unset' command, register variables so they're declared */
    if(n>=2 && !strcmp(toks[0],"unset")){
        for(int i=1;i<n;i++){
            if(toks[i][0]=='-') continue;
            add_var(toks[i],V_STR);
        }
    }
    return nd;
}

void parse_for_header(const char *src,char *var,char ***list,int *len){
    char tmp[1024]; strncpy(tmp,src,1023); tmp[1023]=0;
    char *p;
    /* Truncate at "; do", ";do", or " do" — but verify "do" is a complete
     * word to avoid matching "dog", "done", "door", etc. (BUG-fix) */
    if((p=strstr(tmp,"; do"))){
        while(p && !(p[4]==0||p[4]==' '||p[4]=='\t'||p[4]==';'))
            p=strstr(p+1,"; do");
        if(p) *p=0;
    } else if((p=strstr(tmp,";do"))){
        while(p && !(p[3]==0||p[3]==' '||p[3]=='\t'||p[3]==';'))
            p=strstr(p+1,";do");
        if(p) *p=0;
    } else {
        p=tmp;
        while((p=strstr(p," do"))){
            if(p[3]==0||p[3]==' '||p[3]=='\t'||p[3]==';'){ *p=0; break; }
            p++;
        }
    }
    sscanf(tmp,"%127s",var);
    char *ls=tmp+strlen(var);
    while(isspace((unsigned char)*ls))ls++;
    if(strncmp(ls,"in",2)==0) ls+=2;
    while(isspace((unsigned char)*ls))ls++;
    *list=malloc(64*sizeof(char*)); *len=0;
    /* parse list items, handling $(...), $((...)), ${...}, quotes */
    char *s=ls;
    while(*s && *len<63){
        while(*s==' '||*s=='\t') s++;
        if(!*s || *s==';') break;
        if(!strncmp(s,"do",2) && (s[2]==' '||s[2]=='\t'||s[2]==0)) break;
        char item[512]; int il=0;
        if(*s=='"'){
            s++;
            while(*s && *s!='"' && il<(int)sizeof(item)-1){
                if(*s=='\\' && *(s+1)) s++;
                item[il++]=*s++;
            }
            if(*s=='"') s++;
        } else if(*s=='\''){
            s++;
            while(*s && *s!='\'' && il<(int)sizeof(item)-1) item[il++]=*s++;
            if(*s=='\'') s++;
        } else if(*s=='$' && *(s+1)=='('){
            /* $(...) or $((...)) — read as single unit */
            item[il++]='$'; s++;
            item[il++]='('; s++;
            int d=1;
            while(*s && d && il<(int)sizeof(item)-1){
                if(*s=='(') d++;
                else if(*s==')') d--;
                item[il++]=*s++;
            }
        } else if(*s=='$' && *(s+1)=='{'){
            /* ${...} — read as single unit */
            item[il++]='$'; s++;
            item[il++]='{'; s++;
            while(*s && *s!='}' && il<(int)sizeof(item)-1) item[il++]=*s++;
            if(*s=='}') item[il++]=*s++;
        } else {
            while(*s && *s!=' ' && *s!='\t' && *s!=';' && il<(int)sizeof(item)-1){
                if(*s=='$' && *(s+1)=='('){
                    /* $(...) inside word */
                    item[il++]='$'; s++;
                    item[il++]='('; s++;
                    int d=1;
                    while(*s && d && il<(int)sizeof(item)-1){
                        if(*s=='(') d++;
                        else if(*s==')') d--;
                        item[il++]=*s++;
                    }
                } else if(*s=='$' && *(s+1)=='{'){
                    item[il++]='$'; s++;
                    item[il++]='{'; s++;
                    while(*s && *s!='}' && il<(int)sizeof(item)-1) item[il++]=*s++;
                    if(*s=='}') item[il++]=*s++;
                } else {
                    item[il++]=*s++;
                }
            }
        }
        item[il]=0;
        if(il>0){
            (*list)[(*len)++]=xstrdup(item);
        }
    }
}

/* Process one logical line (a segment between ; separators).
 * Handles block keywords, assignments, &&/||, pipes, redirects, plain cmds. */
void dispatch_segment(char **toks, int ntoks, int lineno){
    if(ntoks<=0) return;
        const char *kw=toks[0];

        if(!strcmp(kw,"esac")){
            if(blk_top&&blk_top->kind==BLK_CASE) parser_pop();
            return;
        }
        /* BLK_CASE body */
        if(blk_top&&blk_top->kind==BLK_CASE){
            Node *cnd=blk_top->node;
            int pp=-1;
            for(int i=0;i<ntoks;i++) if(!strcmp(toks[i],")")){pp=i;break;}
            if(pp>=1){
                int ci=cnd->case_count;
                if(ci<64){
                    if(!strcmp(toks[0],"*")){
                        cnd->case_default=NULL;
                        parse_insert=&cnd->case_default;
                    } else {
                        /* Collect all tokens before ) as the pattern */
                        char pat[512]="";
                        for(int i=0;i<pp;i++){
                            if(i>0) strncat(pat,"",sizeof(pat)-strlen(pat)-1);
                            strncat(pat,toks[i],sizeof(pat)-strlen(pat)-1);
                        }
                        cnd->case_pats[ci]=xstrdup(pat);
                        cnd->case_bodies[ci]=NULL;
                        cnd->case_count++;
                        parse_insert=&cnd->case_bodies[ci];
                    }
                    int cs=pp+1,dsi=-1;
                    for(int i=cs;i<ntoks;i++) if(!strcmp(toks[i],";;")){dsi=i;break;}
                    int ce=(dsi>=0)?dsi:ntoks;
                    if(ce>cs){
                        /* Dispatch each ; separated sub-segment in case body */
                        int __cs2=cs;
                        while(__cs2<ce){
                            int __ce2=ce; int __d2=0;
                            for(int i=__cs2;i<ce;i++){
                                if(!strcmp(toks[i],"[")||!strcmp(toks[i],"(")||!strcmp(toks[i],"[[")) __d2++;
                                else if(!strcmp(toks[i],"]")||!strcmp(toks[i],")")||!strcmp(toks[i],"]]")) __d2--;
                                else if(!__d2 && !strcmp(toks[i],";")){ __ce2=i; break; }
                            }
                            if(__ce2>__cs2) dispatch_segment(toks+__cs2, __ce2-__cs2, lineno);
                            __cs2=__ce2+1;
                        }
                    }
                    if(dsi>=0) parse_insert=NULL;
                }
                return;
            }
            if(!strcmp(toks[0],";;")){parse_insert=NULL;return;}
            if(!parse_insert) return;
        }

        /* (( expr )) — standalone arithmetic command (assignment or test) */
        if(!strcmp(kw,"((")){
            char expr[2048]="";
            for(int i=1;i<ntoks;i++){
                if(!strcmp(toks[i],"))")) break;
                if(i>1) strcat(expr," ");
                strncat(expr,toks[i],sizeof(expr)-strlen(expr)-1);
            }
            /* scan for var= or var++ or var-- and register as int */
            {
                const char *p=expr;
                while(*p){
                    if(isalpha((unsigned char)*p)||*p=='_'){
                        char vn[128]; int vi=0;
                        while(isalnum((unsigned char)*p)||*p=='_'){
                            if(vi<(int)sizeof(vn)-1) vn[vi++]=*p;
                            p++;
                        }
                        vn[vi]=0;
                        /* skip whitespace before checking for = */
                        const char *np=p;
                        while(*np==' '||*np=='\t') np++;
                        if(*np=='='&&*(np+1)!='='){
                            add_var(vn,V_INT);
                        }
                    } else p++;
                }
            }
            /* translate as arithmetic expression, set __exit_status */
            /* Save raw expr for re-translation at emit time */
            char arith[2100];
            snprintf(arith,sizeof(arith),"$((%s))",expr);
            char *e=translate_expr(arith);
            Node *nd=new_node(NODE_CMD,lineno);
            nd->argv=malloc(3*sizeof(char*));
            nd->argv[0]=xstrdup("__arith");
            /* Store raw bash expression in argv[1] for re-translation at emit time */
            nd->argv[1]=xstrdup(expr);
            nd->argc=2;
            nd->argv[2]=NULL;
            free(e);
            parser_append(nd); return;
        }

        if(!strcmp(kw,"if")){
            char cb[2048]="";
            for(int i=1;i<ntoks;i++){
                if(!strcmp(toks[i],"then")||!strcmp(toks[i],";")) break;
                if(i>1) strcat(cb," ");
                strncat(cb,toks[i],sizeof(cb)-strlen(cb)-1);
            }
            Node *nd=new_node(NODE_IF,lineno);
            nd->cond=xstrdup(rtrim(cb));
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_IF_THEN,nd,&nd->then_blk,_pi};
            parser_push(fr); parse_insert=&nd->then_blk; return;
        }
        if(!strcmp(kw,"then")){
            /* If there are more tokens after 'then', dispatch them as body */
            if(ntoks>1){
                dispatch_segment(toks+1,ntoks-1,lineno);
            }
            return;
        }
        if(!strcmp(kw,"elif")){
            if(blk_top){
                BlkFrame *fr=blk_top; Node *pif=fr->node;
                char cb[2048]="";
                for(int i=1;i<ntoks;i++){
                    if(!strcmp(toks[i],"then")||!strcmp(toks[i],";")) break;
                    if(i>1) strcat(cb," ");
                    strncat(cb,toks[i],sizeof(cb)-strlen(cb)-1);
                }
                int ec=pif->elif_count;
                if(ec<16){
                    pif->elif_conds[ec]=new_node(NODE_IF,lineno);
                    pif->elif_conds[ec]->cond=xstrdup(rtrim(cb));
                    pif->elif_blks[ec]=NULL;
                    pif->elif_count++;
                    fr->kind=BLK_IF_ELIF;
                    parse_insert=&pif->elif_blks[ec];
                }
            } return;
        }
        if(!strcmp(kw,"else")){
            if(blk_top){
                BlkFrame *fr=blk_top; Node *pif=fr->node;
                fr->kind=BLK_IF_ELSE;
                parse_insert=&pif->else_blk;
                /* Dispatch tokens after 'else' on same line (e.g., "else echo x; fi") */
                if(ntoks>1){
                    dispatch_segment(toks+1, ntoks-1, lineno);
                }
            } return;
        }
        if(!strcmp(kw,"fi")){ parser_pop(); return; }

        if(!strcmp(kw,"for")){
            /* C-style: for ((i=0; i<n; i++)) */
            if(ntoks>=2 && !strcmp(toks[1],"((")){
                Node *nd=new_node(NODE_FOR,lineno);
                nd->for_c_style=1;
                /* gather the (( ... )) content */
                char buf[1024]="";
                for(int i=2;i<ntoks;i++){
                    if(!strcmp(toks[i],"))")) break;
                    if(i>2) strcat(buf," ");
                    strncat(buf,toks[i],sizeof(buf)-strlen(buf)-1);
                }
                /* split on ; */
                char *p1=strstr(buf,";");
                char init_str[512]="", cond_str[512]="", upd_str[512]="";
                if(p1){
                    int ilen=(int)(p1-buf);
                    if(ilen>=(int)sizeof(init_str)) ilen=(int)sizeof(init_str)-1;
                    memcpy(init_str,buf,ilen); init_str[ilen]=0;
                    char *p2=strstr(p1+1,";");
                    if(p2){
                        int clen=(int)(p2-p1-1);
                        if(clen>=(int)sizeof(cond_str)) clen=(int)sizeof(cond_str)-1;
                        memcpy(cond_str,p1+1,clen); cond_str[clen]=0;
                        strncpy(upd_str,p2+1,sizeof(upd_str)-1); upd_str[sizeof(upd_str)-1]=0;
                    } else {
                        strncpy(cond_str,p1+1,sizeof(cond_str)-1); cond_str[sizeof(cond_str)-1]=0;
                    }
                } else {
                    snprintf(init_str,sizeof(init_str),"%s",buf);
                }
                /* Extract variable name from init (e.g., "i=0" → "i") */
                {
                    const char *ip=init_str;
                    while(*ip==' '||*ip=='\t') ip++;
                    if(isalpha((unsigned char)*ip)||*ip=='_'){
                        char vn[128]; int vi=0;
                        while(isalnum((unsigned char)*ip)||*ip=='_'){
                            if(vi<(int)sizeof(vn)-1) vn[vi++]=*ip;
                            ip++;
                        }
                        vn[vi]=0;
                        nd->for_var=xstrdup(vn);
                        /* Don't force V_INT — the variable might be used as
                         * a string elsewhere. translate_arith handles both. */
                        if(!is_known_var(vn)) add_var(vn,V_INT);
                    } else {
                        nd->for_var=xstrdup("__i");
                        add_var("__i",V_INT);
                    }
                }
                /* Translate init/cond/update from shell arithmetic to C */
                nd->for_init=translate_arith(init_str);
                nd->for_cond=translate_arith(cond_str);
                nd->for_update=translate_arith(upd_str);
                nd->for_init_raw=xstrdup(init_str);
                nd->for_cond_raw=xstrdup(cond_str);
                nd->for_update_raw=xstrdup(upd_str);
                Node **_pi=parse_insert;
                parser_append(nd);
                BlkFrame fr={BLK_FOR,nd,&nd->body,_pi};
                parser_push(fr); parse_insert=&nd->body; return;
            }
            char rest[2048]="";
            for(int i=1;i<ntoks;i++){
                if(i>1) strcat(rest," ");
                strncat(rest,toks[i],sizeof(rest)-strlen(rest)-1);
            }
            Node *nd=new_node(NODE_FOR,lineno);
            char var[128]="";
            parse_for_header(rest,var,&nd->for_list,&nd->for_len);
            nd->for_var=xstrdup(var); add_var(var,V_STR);
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_FOR,nd,&nd->body,_pi};
            parser_push(fr); parse_insert=&nd->body; return;
        }
        if(!strcmp(kw,"do")){
            /* If there are more tokens after 'do', dispatch them as body */
            if(ntoks>1){
                dispatch_segment(toks+1,ntoks-1,lineno);
            }
            return;
        }
        if(!strcmp(kw,"while")||!strcmp(kw,"until")){
            char cb[2048]="";
            for(int i=1;i<ntoks;i++){
                if(!strcmp(toks[i],"do")||!strcmp(toks[i],";")) break;
                /* skip VAR=VALUE prefixes but keep them in condition for emit */
                if(is_assignment(toks[i]) && i<ntoks-1 && strcmp(toks[i+1],"do")!=0 && strcmp(toks[i+1],";")!=0){
                    /* Don't skip — include in condition for emit-time processing */
                }
                /* register read variables */
                if(!strcmp(toks[i],"read")){
                    for(int j=i+1;j<ntoks;j++){
                        if(!strcmp(toks[j],"do")||!strcmp(toks[j],";")) break;
                        if(toks[j][0]=='-') continue;
                        add_var(toks[j],V_STR);
                    }
                }
                if(cb[0]) strcat(cb," ");
                strncat(cb,toks[i],sizeof(cb)-strlen(cb)-1);
            }
            Node *nd=new_node(NODE_WHILE,lineno);
            nd->while_cond=xstrdup(rtrim(cb));
            nd->while_negate=(!strcmp(kw,"until"));
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_WHILE,nd,&nd->while_body,_pi};
            parser_push(fr); parse_insert=&nd->while_body; return;
        }
        /* select var in list; do ... done */
        if(!strcmp(kw,"select")){
            Node *nd=new_node(NODE_SELECT,lineno);
            /* parse: select var in item1 item2 ... */
            if(ntoks>=4 && !strcmp(toks[2],"in")){
                nd->for_var=xstrdup(toks[1]);
                add_var(toks[1],V_STR);
                nd->for_list=malloc(sizeof(char*)*(ntoks-2));
                nd->for_len=0;
                for(int i=3;i<ntoks;i++){
                    if(!strcmp(toks[i],"do")) break;
                    nd->for_list[nd->for_len++]=xstrdup(toks[i]);
                }
            }
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_SELECT,nd,&nd->body,_pi};
            parser_push(fr); parse_insert=&nd->body; return;
        }
        if(!strcmp(kw,"done")){
            /* Check for redirect after done (e.g., done < file, done << HD) */
            if(blk_top && (blk_top->kind==BLK_WHILE||blk_top->kind==BLK_UNTIL)){
                Node *wnd=blk_top->node;
                /* Check for < file redirect */
                if(ntoks>2){
                    for(int i=1;i+1<ntoks;i++){
                        if(!strcmp(toks[i],"<")){
                            Redir *r=new_redir(0,toks[i+1],0,-1,0,0,0);
                            if(!wnd->redirs) wnd->redirs=r;
                            else { Redir *p=wnd->redirs; while(p->next)p=p->next; p->next=r; }
                        }
                    }
                }
                /* Check for unconsumed heredoc (<<HD was pre-scanned and removed) */
                if(heredoc_peek()){
                    int hd_expand=1;
                    const char *hd_text=heredoc_consume(&hd_expand);
                    Redir *r=new_redir(0,NULL,0,-1,1,0,hd_text);
                    r->hd_expand=hd_expand;
                    if(!wnd->redirs) wnd->redirs=r;
                    else { Redir *p=wnd->redirs; while(p->next)p=p->next; p->next=r; }
                }
            }
            parser_pop(); return;
        }

        if(!strcmp(kw,"case")){
            Node *nd=new_node(NODE_CASE,lineno);
            nd->case_var=(ntoks>1)?xstrdup(translate_expr(toks[1])):xstrdup("\"\"");
            nd->case_var_raw=(ntoks>1)?xstrdup(toks[1]):xstrdup("\"\"");
            /* BUG-34 fix: if case_var is an int variable, convert to string */
            if(ntoks>1 && toks[1][0]=='$'){
                const char *vname=toks[1]+1;
                if(*vname=='{') vname++;
                VarKind vk=get_var_kind(vname);
                if(vk==V_INT){
                    /* Wrap in __sh_fmt to get string representation */
                    char tmp[300]; snprintf(tmp,sizeof(tmp),"__sh_fmt(\"%%d\",%s)",safe_cname(vname));
                    free(nd->case_var);
                    nd->case_var=xstrdup(tmp);
                }
            }
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_CASE,nd,NULL,_pi};
            parser_push(fr); parse_insert=NULL;
            /* If 'in' is in the same segment, dispatch remaining tokens */
            int in_idx=-1;
            for(int i=2;i<ntoks;i++){
                if(!strcmp(toks[i],"in")){ in_idx=i; break; }
            }
            if(in_idx>=0 && in_idx+1<ntoks){
                /* Dispatch remaining tokens after 'in' in the same segment */
                dispatch_segment(toks+in_idx+1, ntoks-in_idx-1, lineno);
            }
            return;
        }
        if(!strcmp(kw,"in")) return;

        /* function */
        { int is_func=0; char fname[128]="";
          if(!strcmp(kw,"function")&&ntoks>=2){
              strncpy(fname,toks[1],127);
              int fl=(int)strlen(fname);
              if(fl>=2&&fname[fl-2]=='('&&fname[fl-1]==')') fname[fl-2]=0;
              is_func=1;
          } else {
              char cand[128]; strncpy(cand,kw,127); int cl=(int)strlen(cand);
              if(cl>=2&&cand[cl-2]=='('&&cand[cl-1]==')'){
                  cand[cl-2]=0; snprintf(fname,sizeof(fname),"%s",cand); is_func=1;
              } else if(ntoks>=3&&!strcmp(toks[1],"(")&&!strcmp(toks[2],")")&&!strchr(kw,'=')){
                  strncpy(fname,kw,127); is_func=1;
              }
          }
          if(is_func){
              Node *nd=new_node(NODE_FUNC,lineno); nd->fname=xstrdup(fname);
              register_func(fname);
              Node **_pi=parse_insert;
              parser_append(nd);
              BlkFrame fr={BLK_FUNC,nd,&nd->func_body,_pi};
              parser_push(fr); parse_insert=&nd->func_body;
              /* BUG-01 fix: handle single-line function: func(){ body; }
               * After func() there may be { body; } on the same line.
               * Find { in the token list and dispatch remaining tokens as body. */
              int brace_idx=-1;
              for(int i=0;i<ntoks;i++){
                  if(!strcmp(toks[i],"{")){ brace_idx=i; break; }
              }
              if(brace_idx>=0 && brace_idx+1<ntoks){
                  /* There are tokens after { — dispatch them as function body.
                   * The closing } will be handled by the next segment's "}" check. */
                  /* But if the } is also on this same line (single-line func),
                   * we need to dispatch up to the } and then pop. */
                  int close_idx=-1;
                  for(int i=brace_idx+1;i<ntoks;i++){
                      if(!strcmp(toks[i],"}")){ close_idx=i; break; }
                  }
                  if(close_idx>=0){
                      /* Single-line function: dispatch tokens between { and } */
                      if(close_idx>brace_idx+1)
                          dispatch_segment(toks+brace_idx+1, close_idx-brace_idx-1, lineno);
                      parser_pop();
                  } else {
                      /* Multi-line: { opened, dispatch remaining tokens on this line as body.
                       * The closing } will come from a subsequent line. */
                      if(brace_idx+1<ntoks)
                          dispatch_segment(toks+brace_idx+1, ntoks-brace_idx-1, lineno);
                  }
              }
              return;
          }
        }
        if(!strcmp(kw,"}")){
            if(blk_top&&(blk_top->kind==BLK_FUNC||
                         blk_top->kind==BLK_CASE||
                         blk_top->kind==BLK_GROUP))
                parser_pop();
            return;
        }
        if(!strcmp(kw,"break")){parser_append(new_node(NODE_BREAK,lineno));return;}
        if(!strcmp(kw,"continue")){parser_append(new_node(NODE_CONTINUE,lineno));return;}
        if(!strcmp(kw,"return")){
            Node *nd=new_node(NODE_RETURN,lineno);
            if(ntoks>1){
                /* Check if the return value is a pure integer literal */
                const char *rv=toks[1];
                const char *rp=rv;
                if(*rp=='-'||*rp=='+') rp++;
                int is_num=(*rp!='\0');
                for(const char *q=rp;*q;q++){ if(!isdigit((unsigned char)*q)){ is_num=0; break; } }
                if(is_num)
                    nd->exit_code=atoi(rv);
                else
                    nd->exit_str=xstrdup(rv);
            } else nd->exit_code=-1;
            parser_append(nd);return;}
        if(!strcmp(kw,"exit")){
            Node *nd=new_node(NODE_EXIT,lineno);
            if(ntoks>1) nd->exit_str=xstrdup(toks[1]);
            else nd->exit_code=0;
            parser_append(nd);return;}

        if(!strcmp(kw,"local")||!strcmp(kw,"declare")||!strcmp(kw,"typeset")){
            /* declare/typeset/local: parse flags and variable assignments */
            int is_int=0, is_array=0, is_assoc=0, is_readonly=0; (void)is_readonly;
            int start=1;
            /* parse flags */
            while(start<ntoks && toks[start][0]=='-' && toks[start][1]){
                for(int fi=1;toks[start][fi];fi++){
                    if(toks[start][fi]=='i') is_int=1;
                    else if(toks[start][fi]=='a') is_array=1;
                    else if(toks[start][fi]=='A'){ is_assoc=1; is_array=1; }
                    else if(toks[start][fi]=='r') is_readonly=1;
                }
                start++;
            }
            /* merge name=value tokens (name= and "value" are separate tokens) */
            char merged[128][512]; int nm=0;
            int i=start;
            while(i<ntoks && nm<63){
                char *eq=strchr(toks[i],'=');
                if(eq && !eq[1] && i+1<ntoks){
                    /* name= followed by value token */
                    if(toks[i+1][0]=='('){
                        /* array assignment: name=( elem1 elem2 ... ) */
                        snprintf(merged[nm],sizeof(merged[nm]),"%s%s",toks[i],toks[i+1]);
                        i+=2;
                        while(i<ntoks && strcmp(toks[i],")")){
                            strncat(merged[nm],toks[i],sizeof(merged[nm])-strlen(merged[nm])-1);
                            strncat(merged[nm]," ",sizeof(merged[nm])-strlen(merged[nm])-1);
                            i++;
                        }
                        if(i<ntoks) { strncat(merged[nm],")",sizeof(merged[nm])-strlen(merged[nm])-1); i++; }
                        nm++;
                    } else {
                        snprintf(merged[nm],sizeof(merged[nm]),"%s%s",toks[i],toks[i+1]);
                        nm++; i+=2;
                    }
                } else if(eq && eq[1]){
                    /* name=value in one token */
                    snprintf(merged[nm],sizeof(merged[nm]),"%s",toks[i]);
                    nm++; i++;
                } else if(!strcmp(toks[i],"(")){
                    /* skip ( — part of array assignment already handled */
                    i++;
                } else if(!strcmp(toks[i],")")){
                    /* skip ) — part of array assignment already handled */
                    i++;
                } else {
                    /* just name */
                    snprintf(merged[nm],sizeof(merged[nm]),"%s",toks[i]);
                    nm++; i++;
                }
            }
            /* create NODE_LOCAL with merged tokens */
            char *mtoks[256];
            for(int j=0;j<nm;j++) mtoks[j]=merged[j];
            Node *nd=make_cmd(mtoks,nm,lineno);
            nd->type=NODE_LOCAL;
            for(int j=0;j<nd->argc;j++){
                char *eq=strchr(nd->argv[j],'=');
                if(eq){
                    *eq=0;
                    if(is_assoc) add_var(nd->argv[j],V_ASSOC);
                    else if(is_array || (eq[1]=='('))
                        add_var(nd->argv[j],V_ARRAY);
                    else if(is_int){
                        add_var(nd->argv[j],V_INT);
                        set_declared_int(nd->argv[j]);
                    }
                    else
                        add_var(nd->argv[j],V_STR);
                    *eq='=';
                } else {
                    if(is_assoc) add_var(nd->argv[j],V_ASSOC);
                    else if(is_array) add_var(nd->argv[j],V_ARRAY);
                    else if(is_int){
                        add_var(nd->argv[j],V_INT);
                        set_declared_int(nd->argv[j]);
                    }
                    else add_var(nd->argv[j],V_STR);
                }
            }
            parser_append(nd); return;
        }
        if(!strcmp(kw,"trap")){
            Node *nd=new_node(NODE_TRAP,lineno);
            if(ntoks>=3){
                /* Strip surrounding quotes from trap action */
                char *act=toks[1]; int alen=(int)strlen(act);
                if(alen>=2 && ((act[0]=='\''&&act[alen-1]=='\'') || (act[0]=='"'&&act[alen-1]=='"'))){
                    char *tmp=malloc(alen-1); memcpy(tmp,act+1,alen-2); tmp[alen-2]=0;
                    nd->trap_action=tmp;
                } else nd->trap_action=xstrdup(toks[1]);
                /* Map signal names to numbers */
                if(!strcmp(toks[2],"EXIT")||!strcmp(toks[2],"0")) nd->trap_sig=0;
                else if(!strcmp(toks[2],"INT")||!strcmp(toks[2],"SIGINT")) nd->trap_sig=2;
                else if(!strcmp(toks[2],"TERM")||!strcmp(toks[2],"SIGTERM")) nd->trap_sig=15;
                else if(!strcmp(toks[2],"HUP")||!strcmp(toks[2],"SIGHUP")) nd->trap_sig=1;
                else if(!strcmp(toks[2],"USR1")||!strcmp(toks[2],"SIGUSR1")) nd->trap_sig=10;
                else if(!strcmp(toks[2],"USR2")||!strcmp(toks[2],"SIGUSR2")) nd->trap_sig=12;
                else nd->trap_sig=atoi(toks[2]);
            }
            else if(ntoks>=2){
                char *act=toks[1]; int alen=(int)strlen(act);
                if(alen>=2 && ((act[0]=='\''&&act[alen-1]=='\'') || (act[0]=='"'&&act[alen-1]=='"'))){
                    char *tmp=malloc(alen-1); memcpy(tmp,act+1,alen-2); tmp[alen-2]=0;
                    nd->trap_action=tmp;
                } else nd->trap_action=xstrdup(toks[1]);
                nd->trap_sig=0;
            }
            parser_append(nd); return;
        }

        /* array element assignment: arr[key]=value */
        if(is_array_assignment(toks[0])){
            char aname[128], akey[256], aval[1024]; int aapp=0;
            extract_array_assign(toks[0],aname,sizeof(aname),akey,sizeof(akey),aval,sizeof(aval),&aapp);
            add_var(aname,V_ARRAY);
            Node *nd=new_node(NODE_ASSIGN,lineno);
            nd->lhs=xstrdup(aname);
            /* store key and value in rhs as "key\x01value" */
            char rhs[1280];
            snprintf(rhs,sizeof(rhs),"%s\x01%s",akey,aval);
            nd->rhs=xstrdup(rhs);
            nd->lineno = -3; /* signal array element assignment */
            /* collect remaining tokens for value if needed */
            if(!aval[0] && ntoks>1){
                char fullval[1024]="";
                for(int i=1;i<ntoks;i++){
                    if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                    if(fullval[0]) strncat(fullval," ",sizeof(fullval)-strlen(fullval)-1);
                    strncat(fullval,toks[i],sizeof(fullval)-strlen(fullval)-1);
                }
                snprintf(rhs,sizeof(rhs),"%s\x01%s",akey,fullval);
                free(nd->rhs);
                nd->rhs=xstrdup(rhs);
            }
            parser_append(nd); return;
        }

        /* assignment */
        if(is_assignment(toks[0])){
            /* detect += -= *= /= %= */
            int is_append=0; int append_op=0; /* 0=+, 1=-, 2=*, 3=/, 4=% */
            char *eq=strstr(toks[0],"+=");
            if(eq){ is_append=1; append_op=0; *eq=0; eq++; }
            else { eq=strstr(toks[0],"-="); if(eq){ is_append=1; append_op=1; *eq=0; eq++; } }
            if(!eq){ eq=strstr(toks[0],"*="); if(eq){ is_append=1; append_op=2; *eq=0; eq++; } }
            if(!eq){ eq=strstr(toks[0],"/="); if(eq){ is_append=1; append_op=3; *eq=0; eq++; } }
            if(!eq){ eq=strstr(toks[0],"%="); if(eq){ is_append=1; append_op=4; *eq=0; eq++; } }
            if(!eq){ eq=strchr(toks[0],'='); if(eq){ *eq=0; } }
            Node *nd=new_node(NODE_ASSIGN,lineno);
            nd->lhs=xstrdup(toks[0]);
            if(is_append){
                /* For -=, *=, /=, %= — bash doesn't support these, emit error */
                if(append_op > 0){
                    /* bash: x-=3 → "script: line N: x-=3: command not found" */
                    char cmd[4200];
                    snprintf(cmd,sizeof(cmd),"%s",toks[0]);
                    const char *ops[]={"+","-","*","/","%"};
                    char full_op[8]; snprintf(full_op,sizeof(full_op),"%s=",ops[append_op]);
                    strncat(cmd,full_op,sizeof(cmd)-strlen(cmd)-1);
                    strncat(cmd,eq+1,sizeof(cmd)-strlen(cmd)-1);
                    for(int i=1;i<ntoks;i++){
                        if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                        strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1);
                        strncat(cmd,toks[i],sizeof(cmd)-strlen(cmd)-1);
                    }
                    /* Emit as NODE_ASSIGN with lineno=-5 to signal "command not found" */
                    nd->rhs=xstrdup(cmd);
                    nd->lineno = -lineno - 1000; /* encode original lineno as negative */
                    parser_append(nd);
                    return;
                }
                /* arr+=value — append to variable */
                char rhs[4096]=""; strncpy(rhs,eq+1,4095);
                /* collect remaining tokens */
                for(int i=1;i<ntoks;i++){
                    if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                    if(rhs[0]) strncat(rhs," ",sizeof(rhs)-strlen(rhs)-1);
                    strncat(rhs,toks[i],sizeof(rhs)-strlen(rhs)-1);
                }
                /* check if array append: arr+=(elem1 elem2) */
                if(rhs[0]=='(' || (rhs[0]=='\0' && ntoks>1 && !strcmp(toks[1],"("))){
                    add_var(nd->lhs,V_ARRAY);
                    /* build array append rhs */
                    char arr_rhs[4096]; arr_rhs[0]='('; arr_rhs[1]=0;
                    for(int i=(rhs[0]=='('?1:2);i<ntoks;i++){
                        if(!strcmp(toks[i],";")) break;
                        if(!strcmp(toks[i],"(")) continue;
                        if(!strcmp(toks[i],")")) break;
                        if(arr_rhs[1]!=0) strncat(arr_rhs," ",sizeof(arr_rhs)-strlen(arr_rhs)-1);
                        strncat(arr_rhs,toks[i],sizeof(arr_rhs)-strlen(arr_rhs)-1);
                    }
                    strncat(arr_rhs,")",sizeof(arr_rhs)-strlen(arr_rhs)-1);
                    nd->rhs=xstrdup(arr_rhs);
                    /* mark as append */
                    nd->lineno = -1; /* use lineno=-1 to signal append */
                } else {
                    /* string/numeric append — bash: x+=val does string concat
                     * unless x was declared with declare -i */
                    if(!is_declared_int(nd->lhs)) add_var(nd->lhs,V_STR);
                    nd->rhs=xstrdup(rhs);
                    nd->lineno = -2; /* signal string append */
                }
                parser_append(nd); return;
            }
            char rhs[4096]=""; strncpy(rhs,eq+1,4095);
            /* Convert $'...' ANSI-C quoting to $(echo -e "...") */
            if(rhs[0]=='$' && rhs[1]=='\''){
                char raw[2048]; int ri=0; const char *sp=rhs+2;
                while(*sp && *sp!='\'' && ri<(int)sizeof(raw)-1){
                    if(*sp=='\\' && *(sp+1)=='\''){ raw[ri++]='\''; sp+=2; }
                    else { raw[ri++]=*sp++; }
                }
                raw[ri]=0;
                snprintf(rhs,sizeof(rhs),"$(printf '%%b' \"");
                for(int i=0;raw[i]&&strlen(rhs)<sizeof(rhs)-4;i++){
                    if(raw[i]=='"'){ strncat(rhs,"\\\"",sizeof(rhs)-strlen(rhs)-1); }
                    else if(raw[i]=='\\'){ strncat(rhs,"\\\\",sizeof(rhs)-strlen(rhs)-1); }
                    else { char tmp[2]={raw[i],0}; strncat(rhs,tmp,sizeof(rhs)-strlen(rhs)-1); }
                }
                strncat(rhs,"\")",sizeof(rhs)-strlen(rhs)-1);
            }
            int is_arr=(rhs[0]=='(')||(rhs[0]=='\0'&&ntoks>1&&!strcmp(toks[1],"("));
            if(is_arr){
                char arr_rhs[4096]; arr_rhs[0]='('; arr_rhs[1]=0;
                for(int i=1;i<ntoks;i++){
                    if(!strcmp(toks[i],";")) break;
                    if(!strcmp(toks[i],"(")) continue;
                    if(!strcmp(toks[i],")")) break;
                    if(arr_rhs[1]!=0) strncat(arr_rhs," ",sizeof(arr_rhs)-strlen(arr_rhs)-1);
                    strncat(arr_rhs,toks[i],sizeof(arr_rhs)-strlen(arr_rhs)-1);
                }
                strncat(arr_rhs,")",sizeof(arr_rhs)-strlen(arr_rhs)-1);
                nd->rhs=xstrdup(arr_rhs);
                add_var(nd->lhs,V_ARRAY);
            } else {
                /* Check for VAR=val cmd args pattern (e.g., IFS=: read -ra p) */
                /* Also handle multiple assignments: a=1 b=2 c=3 */
                if(rhs[0] && ntoks>1 && strncmp(rhs,"$((",3)!=0 && strncmp(rhs,"$(",2)!=0){
                    /* Check if remaining tokens are assignments (multi-assign: a=1 b=2) */
                    int is_multi_assign=1;
                    for(int i=1;i<ntoks;i++){
                        if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                        if(!is_assignment(toks[i])){ is_multi_assign=0; break; }
                    }
                    if(is_multi_assign){
                        /* Process each assignment sequentially */
                        nd->rhs=xstrdup(rhs);
                        if(strncmp(rhs,"$((",3)==0) add_var(nd->lhs,V_INT);
                        else if(strncmp(rhs,"$(",2)==0) add_var(nd->lhs,V_STR);
                        else{
                            int inum=1; const char *r2=rhs;
                            if(*r2=='-'||*r2=='+')r2++;
                            if(!*r2)inum=0;
                            while(*r2){if(!isdigit((unsigned char)*r2)){inum=0;break;}r2++;}
                            if(inum&&rhs[0]) add_var(nd->lhs,V_INT);
                            else add_var(nd->lhs,V_STR);
                        }
                        parser_append(nd);
                        /* Dispatch remaining assignments one by one */
                        for(int i=1;i<ntoks;i++){
                            if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                            /* Recursively dispatch each assignment */
                            dispatch_segment(toks+i, 1, lineno);
                        }
                        return;
                    }
                    /* VAR=val is just rhs; rest (toks[1..]) is a command */
                    nd->rhs=xstrdup(rhs);
                    /* Register variable type */
                    if(strncmp(rhs,"$((",3)==0) add_var(nd->lhs,V_INT);
                    else if(strncmp(rhs,"$(",2)==0) add_var(nd->lhs,V_STR);
                    else{
                        int inum=1; const char *r2=rhs;
                        if(*r2=='-'||*r2=='+')r2++;
                        if(!*r2)inum=0;
                        while(*r2){if(!isdigit((unsigned char)*r2)){inum=0;break;}r2++;}
                        if(inum&&rhs[0]) add_var(nd->lhs,V_INT);
                        else add_var(nd->lhs,V_STR);
                    }
                    parser_append(nd);
                    /* Dispatch remaining tokens as a separate command */
                    dispatch_segment(toks+1, ntoks-1, lineno);
                    return;
                }
                for(int i=1;i<ntoks;i++){
                    if(!strcmp(toks[i],";")||!strcmp(toks[i],"then")||!strcmp(toks[i],"do")) break;
                    if(rhs[0]) strncat(rhs," ",sizeof(rhs)-strlen(rhs)-1);
                    strncat(rhs,toks[i],sizeof(rhs)-strlen(rhs)-1);
                }
                nd->rhs=xstrdup(rhs);
                if(strncmp(rhs,"$((",3)==0) add_var(nd->lhs,V_INT);
                else if(strncmp(rhs,"$(",2)==0) add_var(nd->lhs,V_STR);
                else{
                    int inum=1; const char *r2=rhs;
                    if(*r2=='-'||*r2=='+')r2++;
                    if(!*r2)inum=0;
                    while(*r2){if(!isdigit((unsigned char)*r2)){inum=0;break;}r2++;}
                    if(inum&&rhs[0]) add_var(nd->lhs,V_INT);
                    else add_var(nd->lhs,V_STR);
                }
            }
            parser_append(nd); return;
        }

        /* || at top level — left-associative, lower precedence than && */
        {
            int ai=find_op(toks,ntoks,"&&");
            int oi=find_op(toks,ntoks,"||");
            /* || has lower precedence: if || exists, split there first */
            if(oi>=0){
                /* Check if left side starts with [[ — generate if-else */
                if(oi>=2 && !strcmp(toks[0],"[[" )){
                    /* Find ]] in left side */
                    int close_idx=-1;
                    for(int i=1;i<oi;i++){ if(!strcmp(toks[i],"]]")){ close_idx=i; break; } }
                    if(close_idx>=1){
                        char cb[2048]="";
                        for(int i=1;i<close_idx;i++){
                            if(i>1) strcat(cb," ");
                            strncat(cb,toks[i],sizeof(cb)-strlen(cb)-1);
                        }
                        char cond[2100]; snprintf(cond,sizeof(cond),"[[ %s ]]",cb);
                        Node *ifnd=new_node(NODE_IF,lineno);
                        ifnd->cond=xstrdup(cond);
                        BlkFrame ifr={BLK_IF_THEN,ifnd,&ifnd->then_blk,parse_insert};
                        parser_append(ifnd);
                        parser_push(ifr);
                        /* then_blk: tokens between ]] and || (e.g. && echo "nomatch") */
                        if(close_idx+1<oi){
                            parse_insert=&ifnd->then_blk;
                            dispatch_segment(toks+close_idx+1, oi-close_idx-1, lineno);
                        }
                        /* else_blk: right side of || */
                        parse_insert=&ifnd->else_blk;
                        dispatch_segment(toks+oi+1,ntoks-oi-1,lineno);
                        parser_pop();
                        return;
                    }
                }
                Node *nd=new_node(NODE_OR,lineno);
                Node **_pi=parse_insert;
                parser_append(nd);
                BlkFrame fr={BLK_IF_ELSE,nd,&nd->right,_pi};
                parser_push(fr);
                /* Left side may contain && — dispatch recursively */
                parse_insert=&nd->left;
                dispatch_segment(toks,oi,lineno);
                parse_insert=&nd->right;
                dispatch_segment(toks+oi+1,ntoks-oi-1,lineno);
                parser_pop();
                return;
            }
            if(ai>=0){
                Node *nd=new_node(NODE_AND,lineno);
                /* BUG-18fix: if left side is [[ ]], make it an if-condition not a cmd */
                if(ai>=2 && !strcmp(toks[0],"[[") && !strcmp(toks[ai-1],"]]")){
                    /* Build condition string from tokens between [[ and ]] */
                    char cb[2048]="";
                    for(int i=1;i<ai-1;i++){
                        if(i>1) strcat(cb," ");
                        strncat(cb,toks[i],sizeof(cb)-strlen(cb)-1);
                    }
                    char cond[2100]; snprintf(cond,sizeof(cond),"[[ %s ]]",cb);
                    Node *ifnd=new_node(NODE_IF,lineno);
                    ifnd->cond=xstrdup(cond);
                    BlkFrame ifr={BLK_IF_THEN,ifnd,&ifnd->then_blk,parse_insert};
                    parser_append(ifnd);
                    parser_push(ifr);
                    parse_insert=&ifnd->then_blk;
                    /* The && right side becomes the then-body */
                    dispatch_segment(toks+ai+1,ntoks-ai-1,lineno);
                    parser_pop();
                    return;
                }
                nd->left=make_cmd(toks,ai,lineno);
                /* Right side may contain more && — dispatch recursively */
                Node **_pi=parse_insert;
                parser_append(nd);
                BlkFrame fr={BLK_IF_THEN,nd,&nd->right,_pi};
                parser_push(fr);
                parse_insert=&nd->right;
                dispatch_segment(toks+ai+1,ntoks-ai-1,lineno);
                parser_pop();
                return;
            }
        }

        /* pipe — build left-associative chain: ((a|b)|c)|d */
        { int pp=find_op(toks,ntoks,"|");
          if(pp>=0){
              /* Check if the stage after | starts with a block keyword */
              int block_start=-1;
              for(int i=pp+1;i<ntoks;i++){
                  if(!strcmp(toks[i],"while")||!strcmp(toks[i],"until")||
                     !strcmp(toks[i],"for")){
                      block_start=i;
                      break;
                  }
                  if(!strcmp(toks[i],"|")) break; /* another pipe, not block */
              }
              if(block_start>=0){
                  /* cmd | while/until/for ... — set up pipe and parse block */
                  pending_pipe_cmd = make_cmd(toks,pp,lineno);
                  dispatch_segment(toks+block_start,ntoks-block_start,lineno);
                  return;
              }
              /* Check if any stage starts with a block keyword */
              {
              Node *stages[32]; int ns=0;
              int start=0;
              while(start<ntoks && ns<32){
                  int next=-1; int d=0;
                  for(int i=start;i<ntoks;i++){
                      if(!strcmp(toks[i],"[")||!strcmp(toks[i],"[[")||!strcmp(toks[i],"(")) d++;
                      if(!strcmp(toks[i],"]")||!strcmp(toks[i],"]]")||!strcmp(toks[i],")")) d--;
                      if(!d && !strcmp(toks[i],"|")){ next=i; break; }
                  }
                  if(next<0){ stages[ns++]=make_cmd(toks+start,ntoks-start,lineno); break; }
                  stages[ns++]=make_cmd(toks+start,next-start,lineno);
                  start=next+1;
              }
              /* fold left */
              Node *result=stages[0];
              for(int i=1;i<ns;i++){
                  Node *pipe=new_node(NODE_PIPE,lineno);
                  pipe->left=result; pipe->right=stages[i];
                  result=pipe;
              }
              parser_append(result); return;
              }
          }
        }

        /* subshell ( ... ) */
        if(ntoks>=1 && !strcmp(toks[0],"(")){
            /* find matching ) */
            int d=1, end=-1;
            for(int i=1;i<ntoks;i++){
                if(!strcmp(toks[i],"(")) d++;
                else if(!strcmp(toks[i],")")){ d--; if(d==0){end=i;break;} }
            }
            if(end>1){
                Node *nd=new_node(NODE_SUBSHELL,lineno);
                /* Parse inner content as multiple segments (handle ; inside) */
                nd->left=NULL;
                Node **_pi=parse_insert;
                parser_append(nd);
                BlkFrame fr={BLK_IF_THEN,nd,&nd->left,_pi};
                parser_push(fr);
                parse_insert=&nd->left;
                /* Split inner tokens on ; and dispatch each */
                int inner_start=1;
                for(int i=1;i<=end;i++){
                    if(i==end || !strcmp(toks[i],";")){
                        if(i>inner_start){
                            dispatch_segment(toks+inner_start,i-inner_start,lineno);
                        }
                        inner_start=i+1;
                    }
                }
                parser_pop();
                /* If there are tokens after ), handle them */
                if(end+1<ntoks){
                    dispatch_segment(toks+end+1,ntoks-end-1,lineno);
                }
                return;
            }
        }

        /* { ...; } group */
        if(ntoks>=1 && !strcmp(toks[0],"{")){
            Node *nd=new_node(NODE_GROUP,lineno);
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_GROUP,nd,&nd->left,_pi};
            parser_push(fr);
            parse_insert=&nd->left;
            /* Dispatch remaining tokens on same line as body */
            if(ntoks>1){
                /* Split on ; and dispatch each sub-segment */
                int __cs=1;
                while(__cs<ntoks){
                    int __ce=ntoks; int __d=0;
                    for(int i=__cs;i<ntoks;i++){
                        if(!strcmp(toks[i],"[")||!strcmp(toks[i],"(")||!strcmp(toks[i],"[[")) __d++;
                        else if(!strcmp(toks[i],"]")||!strcmp(toks[i],")")||!strcmp(toks[i],"]]")) __d--;
                        else if(!__d && !strcmp(toks[i],";")){ __ce=i; break; }
                        else if(!strcmp(toks[i],"}")){ __ce=i; break; }
                    }
                    if(__ce>__cs) dispatch_segment(toks+__cs, __ce-__cs, lineno);
                    __cs=__ce+1;
                }
            }
            return;
        }

        /* background */
        if(ntoks>0&&!strcmp(toks[ntoks-1],"&")){
            Node *nd=make_cmd(toks,ntoks-1,lineno);
            nd->type=NODE_BACKGROUND; parser_append(nd); return;
        }

        /* plain command */
        parser_append(make_cmd(toks,ntoks,lineno));
}

Node *parse_script(FILE *f){
    char line[16384]; int lineno=0;
    blk_top=0; parse_root=NULL; parse_insert=NULL;
    while(fgets(line,sizeof(line),f)){
        lineno++;
        /* handle line continuation */
        int ll=(int)strlen(line);
        while(ll>=2 && line[ll-2]=='\\'){
            line[ll-2]='\n'; line[ll-1]=0;
            if(!fgets(line+ll-2,(int)sizeof(line)-(ll-2),f)) break;
            lineno++;
            ll=(int)strlen(line);
        }
        /* Handle multi-line quoted strings: if there are unclosed quotes,
         * read more lines until quotes are balanced */
        {
            int quote_closed = 0;
            while(!quote_closed){
                int in_squote=0, in_dquote=0;
                for(int i=0;line[i];i++){
                    if(in_dquote){
                        if(line[i]=='\\'&&line[i+1]) i++;
                        else if(line[i]=='"') in_dquote=0;
                    } else if(in_squote){
                        if(line[i]=='\'') in_squote=0;
                    } else {
                        /* Handle escaped characters outside quotes */
                        if(line[i]=='\\' && line[i+1]) { i++; }
                        else if(line[i]=='"') in_dquote=1;
                        else if(line[i]=='\'') in_squote=1;
                        /* skip comments that start outside quotes */
                        else if(line[i]=='#') break;
                    }
                }
                if(in_dquote || in_squote){
                    /* unclosed quote — read next line */
                    int cl=(int)strlen(line);
                    /* strip trailing newline from current buffer before joining */
                    if(cl>0 && (line[cl-1]=='\n'||line[cl-1]=='\r')) line[--cl]=0;
                    if(cl < (int)sizeof(line)-2){
                        line[cl]='\n'; line[cl+1]=0;
                        if(!fgets(line+cl+1,(int)sizeof(line)-cl-1,f)){
                            quote_closed=1; /* EOF — give up */
                        } else {
                            lineno++;
                        }
                    } else {
                        quote_closed=1; /* buffer full — give up */
                    }
                } else {
                    quote_closed=1;
                }
            }
        }
        /* Remove only the trailing newline (not embedded ones from multi-line quotes) */
        {
            int ll2=(int)strlen(line);
            while(ll2>0 && (line[ll2-1]=='\n'||line[ll2-1]=='\r')) line[--ll2]=0;
        }
        strip_comment(line);
        char *t=ltrim(line); rtrim(t);
        if(!*t) continue;
        /* heredoc pre-scan: find <<DELIM patterns, read bodies, store in table.
         * Multiple heredocs per line are supported (e.g. cat <<A <<B).
         * We DON'T skip the line — it's processed normally after. */
        {
            char *scan=t;
            while((scan=strstr(scan,"<<"))!=NULL){
                /* skip <<< (here-string) */
                if(*(scan+2)=='<'){ scan+=3; continue; }
                /* skip if inside $(...) or $((...)) — command/arithmetic substitution */
                {
                    int pd=0; /* paren depth */
                    int in_subst=0; /* inside $( or $(( */
                    for(char *q=t;q<scan;q++){
                        if(q[0]=='$' && q[1]=='('){
                            in_subst++; pd++;
                            if(q[2]=='('){ pd++; q+=2; }
                            else { q+=1; }
                        }
                        else if(*q=='(') pd++;
                        else if(*q==')') { if(pd>0) pd--; if(in_subst>0 && pd==0) in_subst--; }
                    }
                    if(in_subst>0){
                        /* Check if inside $((...)) (arithmetic) vs $(...) (cmd subst) */
                        int is_arith=0;
                        {
                            int pd3=0; int in_a3=0;
                            for(char *q=t;q<scan;q++){
                                if(q[0]=='$'&&q[1]=='('&&q[2]=='('){
                                    in_a3++; pd3+=2; q+=2;
                                } else if(q[0]=='$'&&q[1]=='('){
                                    pd3++; q+=1;
                                } else if(*q=='(') pd3++;
                                else if(*q==')'){ if(pd3>0)pd3--; if(in_a3>0&&pd3==0) in_a3--; }
                            }
                            if(in_a3>0) is_arith=1;
                        }
                        if(is_arith){
                            /* << is bitwise shift inside $((...)) — skip */
                            scan+=2;
                            continue;
                        }
                        /* else: << is heredoc inside $(...) — append body to line */
                        {
                            char *dp2=scan+2;
                            int strip_tabs2=0;
                            if(*dp2=='-'){ strip_tabs2=1; dp2++; }
                            while(*dp2==' '||*dp2=='\t') dp2++;
                            char delim2[128]; int di2=0;
                            int quoted2=0;
                            if(*dp2=='"'||*dp2=='\''){
                                char qc=*dp2; dp2++; quoted2=1;
                                while(*dp2 && *dp2!=qc && di2<127) delim2[di2++]=*dp2++;
                                if(*dp2==qc) dp2++;
                            } else {
                                while(*dp2 && *dp2!=' ' && *dp2!='\t' && *dp2!=';' && *dp2!='&' && *dp2!='|' && di2<127) delim2[di2++]=*dp2++;
                            }
                            delim2[di2]=0;
                            (void)quoted2; /* quoted delimiter disables expansion in bash;
                                            * here the body is passed to runtime sh -c,
                                            * so expansion is handled by the shell. */
                            if(di2>0){
                                /* Read heredoc body and append to current line */
                                char hdline2[2048];
                                int cur_len = strlen(t);
                                while(fgets(hdline2,sizeof(hdline2),f)){
                                    lineno++;
                                    hdline2[strcspn(hdline2,"\r\n")]=0;
                                    /* strip leading tabs if <<- was used */
                                    if(strip_tabs2){
                                        char *h=hdline2;
                                        while(*h=='\t') h++;
                                        memmove(hdline2,h,strlen(h)+1);
                                    }
                                    if(strcmp(hdline2,delim2)==0) break;
                                    if(cur_len < (int)sizeof(line)-2){
                                        t[cur_len++]='\n';
                                        int hl=strlen(hdline2);
                                        if(cur_len+hl < (int)sizeof(line)-1){
                                            memcpy(t+cur_len,hdline2,hl);
                                            cur_len+=hl;
                                            t[cur_len]=0;
                                        }
                                    }
                                }
                                /* Also append closing delimiter */
                                if(cur_len < (int)sizeof(line)-2){
                                    t[cur_len++]='\n';
                                    int dl=strlen(delim2);
                                    if(cur_len+dl < (int)sizeof(line)-1){
                                        memcpy(t+cur_len,delim2,dl);
                                        cur_len+=dl;
                                        t[cur_len]=0;
                                    }
                                }
                                /* Check if next line is the closing ) for $(...) */
                                {
                                    char peek[256];
                                    if(fgets(peek,sizeof(peek),f)){
                                        /* strip newline */
                                        int pl=strlen(peek);
                                        while(pl>0 && (peek[pl-1]=='\n'||peek[pl-1]=='\r')) peek[--pl]=0;
                                        /* If it's just ), append to current line */
                                        if(strcmp(peek,")")==0 && cur_len<(int)sizeof(line)-2){
                                            t[cur_len++]=')';
                                            t[cur_len]=0;
                                        } else {
                                            /* Not ), push back as next line */
                                            /* Re-add newline */
                                            peek[pl]='\n'; peek[pl+1]=0;
                                            /* Can't ungetc a whole line, but we can prepend to a buffer */
                                            /* Simple approach: process as part of current line */
                                            if(cur_len+pl+1 < (int)sizeof(line)-1){
                                                t[cur_len++]='\n';
                                                memcpy(t+cur_len,peek,pl+1);
                                                cur_len+=pl;
                                                t[cur_len]=0;
                                            }
                                        }
                                        lineno++;
                                    }
                                }
                            }
                            scan+=2;
                            continue;
                        }
                    }
                    /* Normal heredoc processing (not inside $(...)) */
                    {
                        char *dp=scan+2;
                        int strip_tabs=0;
                        if(*dp=='-'){ strip_tabs=1; dp++; }
                        while(*dp==' '||*dp=='\t') dp++;
                        char delim[128]; int di=0;
                        int quoted=0;
                        if(*dp=='"'||*dp=='\''){
                            char qc=*dp; dp++; quoted=1;
                            while(*dp && *dp!=qc && di<127) delim[di++]=*dp++;
                            if(*dp==qc) dp++;
                        } else {
                            while(*dp && *dp!=' ' && *dp!='\t' && *dp!=';' && *dp!='&' && *dp!='|' && di<127) delim[di++]=*dp++;
                        }
                        delim[di]=0;
                        if(di>0){
                            char hdtext[65536]=""; char hdline[8192];
                            while(fgets(hdline,sizeof(hdline),f)){
                                lineno++;
                                hdline[strcspn(hdline,"\r\n")]=0;
                                char *trimmed = strip_tabs ? hdline : hdline;
                                if(strip_tabs){
                                    char *h=hdline;
                                    while(*h=='\t') h++;
                                    trimmed=h;
                                } else trimmed=hdline;
                                if(strcmp(trimmed,delim)==0) break;
                                /* BUG-09 fix: use trimmed (tab-stripped) line when <<- is used */
                                strncat(hdtext,trimmed,sizeof(hdtext)-strlen(hdtext)-2);
                                strcat(hdtext,"\n");
                            }
                            heredoc_store(hdtext, !quoted);
                            memmove(scan+2,dp,strlen(dp)+1);
                            scan+=2;
                        } else {
                            scan+=2;
                        }
                        continue;
                    }
                }
                /* Old heredoc code removed — replaced by new code above */
                scan+=2;
            }
        }
        char linecopy[16384]; strncpy(linecopy,t,16383); linecopy[16383]=0;
        char *toks[2048]; int ntoks=tokenize(linecopy,toks,2048);
        if(!ntoks) continue;
        /* expand brace patterns */
        ntoks=expand_braces(toks,ntoks,2048);
        /* split on top-level ';' and dispatch each segment */
        {
            int __ss=0;
            while(__ss<ntoks){
                int __se=ntoks, __d=0;
                for(int __i=__ss;__i<ntoks;__i++){
                    if(!strcmp(toks[__i],"[")||!strcmp(toks[__i],"[[")||!strcmp(toks[__i],"(")||!strcmp(toks[__i],"((")) __d++;
                    else if(!strcmp(toks[__i],"]")||!strcmp(toks[__i],"]]")||!strcmp(toks[__i],")")||!strcmp(toks[__i],"))")) __d--;
                    else if(!__d && !strcmp(toks[__i],";")){ __se=__i; break; }
                }
                if(__se>__ss) dispatch_segment(toks+__ss, __se-__ss, lineno);
                __ss=__se+1;
            }
            continue;
        }
    }
    return parse_root;
}

