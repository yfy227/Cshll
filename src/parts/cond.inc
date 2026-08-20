/* ================================================================== */
/* L7  Condition translator  ([ ], [[ ]])                             */
/* ================================================================== */

char *translate_cond(const char *cond);

/* Translate a single test operand to a C expression string (heap). */
char *translate_operand(const char *tok){
    if(!tok||!*tok) return xstrdup("\"\"");
    int len=(int)strlen(tok);
    /* double-quoted: strip quotes, translate inner; if inner is a pure
     * literal (no $), wrap result as a C string literal */
    if(len>=2 && tok[0]=='"' && tok[len-1]=='"'){
        char inner[1024];
        int n=len-2; if(n>=(int)sizeof(inner)) n=(int)sizeof(inner)-1;
        memcpy(inner,tok+1,n); inner[n]=0;
        char *e=translate_expr(inner);
        if(strchr(inner,'$')==NULL && strncmp(inner,"$(",2)!=0){
            char *r=malloc(strlen(e)+4);
            sprintf(r,"\"%s\"",e); free(e); return r;
        }
        return e;
    }
    /* single-quoted: always a literal string */
    if(len>=2 && tok[0]=='\'' && tok[len-1]=='\''){
        char *r=malloc(len+2);
        memcpy(r+1,tok+1,len-2); r[0]='"'; r[len-1]='"'; r[len]=0;
        return r;
    }
    /* unquoted: translate, and if it's a bare literal word (no $, not a
     * known variable, not a number) wrap as C string literal */
    char *e=translate_expr(tok);
    if(tok[0]!='$' && !strchr(tok,'$') && strncmp(tok,"$((",3)!=0){
        const char *p=tok; if(*p=='-'||*p=='+') p++;
        int isnum=*p!='\0';
        for(const char *q=p;*q;q++){ if(!isdigit((unsigned char)*q)){ isnum=0; break; } }
        /* Check if this is a real declared variable (not a false-positive registration) */
        int is_real_var = is_known_var(tok) && (tok[0]=='_' || (tok[0]>='a'&&tok[0]<='z') || (tok[0]>='A'&&tok[0]<='Z'));
        /* In [[ ]] context, bare words are string literals unless they're clearly variables
         * that were explicitly assigned (V_STR vars assigned with =, or V_INT with declare -i) */
        if(!isnum && !is_real_var){
            char *r=malloc(strlen(e)+4);
            sprintf(r,"\"%s\"",e); free(e); return r;
        }
    }
    return e;
}

/* Quote a path-like operand if it looks like a bare path */
static char *quote_if_path(const char *c){
    if(c[0]=='/' || (c[0]=='.'&&c[1]=='/')){
        char *r=malloc(strlen(c)+4);
        sprintf(r,"\"%s\"",c);
        return r;
    }
    return xstrdup(c);
}

/* Build a C expression of type char* for a shell word used as a function
 * argument in __av[] arrays. Guarantees type correctness:
 *  - "$..." words go through translate_expr (char* expansion)
 *  - quoted words with $ inside go through translate_expr (__sh_fmt)
 *  - single-quoted / bare words (incl. numbers) become escaped C literals
 * This is unlike translate_operand/translate_expr, which intentionally
 * return bare numeric literals for arithmetic contexts. */
static char *av_arg_expr(const char *word){
    int len = word ? (int)strlen(word) : 0;
    if(!word || !len) return xstrdup("\"\"");
    /* $-prefixed: full expansion via translate_expr (returns char* expr) */
    if(word[0]=='$') return translate_expr(word);
    /* double-quoted containing $: expansion needed */
    if(len>=2 && word[0]=='"' && word[len-1]=='"' && strchr(word,'$'))
        return translate_expr(word);
    /* single-quoted: literal (strip quotes, escape, no expansion) */
    /* double-quoted without $: literal (strip quotes, escape) */
    /* bare word: literal (escape) */
    const char *inner = word; int ilen = len;
    if(len>=2 && ((word[0]=='"'&&word[len-1]=='"')||(word[0]=='\''&&word[len-1]=='\''))){
        inner = word+1; ilen = len-2;
    }
    char *r = malloc(ilen*4+8); if(!r) return xstrdup("\"\"");
    int q=0; r[q++]='"';
    for(int i=0;i<ilen;i++){
        char c=inner[i];
        if(c=='"'||c=='\\'){ r[q++]='\\'; r[q++]=c; }
        else if(c=='\n'){ r[q++]='\\'; r[q++]='n'; }
        else if(c=='\t'){ r[q++]='\\'; r[q++]='t'; }
        else if(c=='\r'){ r[q++]='\\'; r[q++]='r'; }
        else if(isprint((unsigned char)c)||c&0x80){ r[q++]=c; }
        else { q+=snprintf(r+q,8,"\\%03o",(unsigned char)c); }
    }
    r[q++]='"'; r[q]=0;
    return r;
}

char *translate_test_unary(const char *op,const char *a1){
    static char buf[1024];
    /* For -n/-z with int variables, use numeric check instead of string */
    if(!strcmp(op,"-n") || !strcmp(op,"-z")){
        /* Check if operand is a simple $var or "var" that's an int */
        const char *check = a1;
        if(check[0]=='"') check++; /* skip quote */
        if(check[0]=='$') check++;
        char vname[128]; int vi=0;
        while(*check && (isalnum((unsigned char)*check)||*check=='_') && vi<127)
            vname[vi++]=*check++;
        vname[vi]=0;
        if(vi>0 && get_var_kind(vname)==V_INT){
            /* Int variable: -n means != 0, -z means == 0 */
            const char *cn=safe_cname(vname);
            if(!strcmp(op,"-n")) snprintf(buf,sizeof(buf),"(%s != 0)",cn);
            else snprintf(buf,sizeof(buf),"(%s == 0)",cn);
            return buf;
        }
    }
    char *c1=translate_operand(a1);
    char *q1=quote_if_path(c1); free(c1);
    if(!strcmp(op,"-z"))      snprintf(buf,sizeof(buf),"(%s[0]=='\\0')",q1);
    else if(!strcmp(op,"-n")) snprintf(buf,sizeof(buf),"(%s[0]!='\\0')",q1);
    else if(!strcmp(op,"-f")||!strcmp(op,"-e"))
        snprintf(buf,sizeof(buf),"(__sh_test_file(%s,0))",q1);
    else if(!strcmp(op,"-d")) snprintf(buf,sizeof(buf),"(__sh_test_file(%s,1))",q1);
    else if(!strcmp(op,"-r")) snprintf(buf,sizeof(buf),"(access(%s,R_OK)==0)",q1);
    else if(!strcmp(op,"-w")) snprintf(buf,sizeof(buf),"(access(%s,W_OK)==0)",q1);
    else if(!strcmp(op,"-x")) snprintf(buf,sizeof(buf),"(access(%s,X_OK)==0)",q1);
    else if(!strcmp(op,"-s")) snprintf(buf,sizeof(buf),"(__sh_test_sfile(%s))",q1);
    else if(!strcmp(op,"-h")||!strcmp(op,"-L")) snprintf(buf,sizeof(buf),"(__sh_test_link(%s))",q1);
    else if(!strcmp(op,"-p")) snprintf(buf,sizeof(buf),"(__sh_test_fifo(%s))",q1);
    else if(!strcmp(op,"-S")) snprintf(buf,sizeof(buf),"(__sh_test_sock(%s))",q1);
    else if(!strcmp(op,"-b")) snprintf(buf,sizeof(buf),"(__sh_test_blk(%s))",q1);
    else if(!strcmp(op,"-c")) snprintf(buf,sizeof(buf),"(__sh_test_chr(%s))",q1);
    else if(!strcmp(op,"-t")) snprintf(buf,sizeof(buf),"(isatty(%s?atoi(%s):0))",q1,q1);
    else if(!strcmp(op,"-g")) snprintf(buf,sizeof(buf),"(__sh_test_mode(%s,S_ISGID))",q1);
    else if(!strcmp(op,"-u")) snprintf(buf,sizeof(buf),"(__sh_test_mode(%s,S_ISUID))",q1);
    else if(!strcmp(op,"-k")) snprintf(buf,sizeof(buf),"(__sh_test_mode(%s,S_ISVTX))",q1);
    else if(!strcmp(op,"-v")) snprintf(buf,sizeof(buf),"(__sh_test_var(%s))",q1);
    else if(!strcmp(op,"-O")) snprintf(buf,sizeof(buf),"(__sh_test_owner(%s))",q1);
    else if(!strcmp(op,"-G")) snprintf(buf,sizeof(buf),"(__sh_test_group(%s))",q1);
    else if(!strcmp(op,"-N")) snprintf(buf,sizeof(buf),"(__sh_test_newer(%s))",q1);
    else snprintf(buf,sizeof(buf),"(%s[0]!='\\0')",q1);
    free(q1);
    return buf;
}

/* Translate an operand for a NUMERIC comparison context.
 * Returns a C expression of type int (no atoi wrapping needed). */
char *translate_num_operand(const char *tok){
    if(!tok||!*tok) return xstrdup("0");
    /* $(( expr )) — arithmetic, already int */
    if(strncmp(tok,"$((",3)==0) return translate_expr(tok);
    /* $( cmd ) — command substitution returns char*, atoi it */
    if(strncmp(tok,"$(",2)==0){
        char *e=translate_expr(tok);
        char *r=malloc(strlen(e)+16);
        sprintf(r,"atoi(%s)",e); free(e); return r;
    }
    if(tok[0]=='$'){
        const char *p=tok+1;
        if(*p=='{'){
            /* ${var...} — handle ${#var} (length, already int) and ${var} */
            char body[256]; int k=0; p++;
            while(*p&&*p!='}') body[k++]=*p++;
            body[k]=0;
            /* ${#var} — string length, already int */
            if(body[0]=='#'){
                char nm[128]; int j=0; const char *bp=body+1;
                while(*bp && j<(int)sizeof(nm)-1) nm[j++]=*bp++;
                nm[j]=0;
                char r[256]; snprintf(r,sizeof(r),"(int)strlen(%s)",safe_cname(nm));
                return xstrdup(r);
            }
            /* plain ${var} */
            if(get_var_kind(body)==V_INT) return xstrdup(safe_cname(body));
            char *e=translate_expr(tok);
            char *r=malloc(strlen(e)+16);
            sprintf(r,"atoi(%s)",e); free(e); return r;
        }
        if(*p=='?') return xstrdup("__exit_status");
        if(*p=='#') return xstrdup("__sh_argc");
        if(*p=='$') return xstrdup("(int)getpid()");
        if(*p=='!') return xstrdup("__sh_last_bg_pid");
        if(isdigit((unsigned char)*p)){
            char r[32]; snprintf(r,sizeof(r),"atoi(__sh_arg%c)",*p);
            return xstrdup(r);
        }
        char name[256]; int j=0;
        while(isalnum((unsigned char)*p)||*p=='_') name[j++]=*p++;
        name[j]=0;
        if(get_var_kind(name)==V_INT) return xstrdup(safe_cname(name));
        /* string var → atoi */
        char r[300]; snprintf(r,sizeof(r),"atoi(%s)",safe_cname(name));
        return xstrdup(r);
    }
    /* bare token: int var, integer literal, or string */
    if(get_var_kind(tok)==V_INT) return xstrdup(safe_cname(tok));
    /* integer literal? */
    const char *p2=tok;
    if(*p2=='-'||*p2=='+') p2++;
    int alldigit=*p2!='\0';
    for(const char *q=p2;*q;q++){ if(!isdigit((unsigned char)*q)){ alldigit=0; break; } }
    if(alldigit) return xstrdup(tok);
    /* otherwise treat as string → atoi */
    char *e=translate_expr(tok);
    char *r=malloc(strlen(e)+16);
    sprintf(r,"atoi(%s)",e); free(e); return r;
}

char *translate_test_binary(const char *op,const char *a1,const char *a2){
    static char buf[2048];
    char *c1=translate_operand(a1);
    char *c2=translate_operand(a2);
    char *q1=quote_if_path(c1); free(c1);
    char *q2=quote_if_path(c2); free(c2);
    /* For numeric ops, use translate_num_operand to avoid atoi() on ints */
    if(!strcmp(op,"-gt")||!strcmp(op,"-lt")||!strcmp(op,"-ge")||
       !strcmp(op,"-le")||!strcmp(op,"-eq")||!strcmp(op,"-ne")){
        char *n1=translate_num_operand(a1);
        char *n2=translate_num_operand(a2);
        const char *cop = !strcmp(op,"-gt")?">":!strcmp(op,"-lt")?"<":
                          !strcmp(op,"-ge")?">=":!strcmp(op,"-le")?"<=":
                          !strcmp(op,"-eq")?"==":"!=";
        snprintf(buf,sizeof(buf),"(%s %s %s)",n1,cop,n2);
        free(n1); free(n2); free(q1); free(q2);
        return buf;
    }
    if(!strcmp(op,"=")||!strcmp(op,"=="))
        snprintf(buf,sizeof(buf),"(strcmp(%s, %s) == 0)",q1,q2);
    else if(!strcmp(op,"!="))
        snprintf(buf,sizeof(buf),"(strcmp(%s, %s) != 0)",q1,q2);
    else if(!strcmp(op,"<")||!strcmp(op,"\\<"))
        snprintf(buf,sizeof(buf),"(strcmp(%s, %s) < 0)",q1,q2);
    else if(!strcmp(op,">")||!strcmp(op,"\\>"))
        snprintf(buf,sizeof(buf),"(strcmp(%s, %s) > 0)",q1,q2);
    else if(!strcmp(op,"-ef"))
        snprintf(buf,sizeof(buf),"(__sh_test_same(%s, %s))",q1,q2);
    else if(!strcmp(op,"-nt"))
        snprintf(buf,sizeof(buf),"(__sh_test_nt(%s, %s))",q1,q2);
    else if(!strcmp(op,"-ot"))
        snprintf(buf,sizeof(buf),"(__sh_test_ot(%s, %s))",q1,q2);
    else snprintf(buf,sizeof(buf),"(strcmp(%s, %s) == 0)",q1,q2);
    free(q1); free(q2);
    return buf;
}

char *translate_cond(const char *cond){
    static char buf[4096];
    buf[0]=0; /* clear static buffer from previous calls */
    const char *s=cond;
    while(isspace((unsigned char)*s)) s++;
    /* [ ... ] — single bracket test, treat same as [[ ]] */
    if(s[0]=='[' && s[1]!='[' && (isspace((unsigned char)s[1])||s[1]==0)){
        /* Only handle simple [ expr ] — not [ ... ] && [ ... ] */
        /* Check if there's a matching ] at the end */
        int has_close=0;
        { const char *q=s+1; int dq=0;
          while(*q){
            if(*q=='"'&&dq==0) dq=1;
            else if(*q=='"'&&dq==1) dq=0;
            else if(!dq && *q==']' && (*(q+1)==0||isspace((unsigned char)*(q+1)))) { has_close=1; break; }
            q++;
          }
        }
        if(has_close){
        s++; while(isspace((unsigned char)*s)) s++;
        char tmp2[2048]; strncpy(tmp2,s,sizeof(tmp2)-1); tmp2[sizeof(tmp2)-1]=0;
        int rp2=(int)strlen(tmp2);
        while(rp2>0 && isspace((unsigned char)tmp2[rp2-1])) tmp2[--rp2]=0;
        if(rp2>=1 && tmp2[rp2-1]==']') tmp2[--rp2]=0;
        /* Now process tmp2 same as [[ ]] body */
        /* Custom tokenizer that protects $((...)), $(...), ${...}, and quotes */
        char *words[256]; int nw=0;
        {
            char *cp=strdup(tmp2);
            const char *p=cp;
            while(*p && nw<255){
                while(*p==' '||*p=='\t') p++;
                if(!*p) break;
                const char *start=p;
                int dq=0,sq=0;
                while(*p){
                    if(*p=='"'&&!sq) dq=!dq;
                    else if(*p=='\''&&!dq) sq=!sq;
                    else if(!dq&&!sq){
                        if(*p=='$'&&(*(p+1)=='('||*(p+1)=='{')){
                            char open=*++p; int d=1; p++;
                            while(*p&&d){
                                if(*p=='('&&open=='(') d++;
                                else if(*p==')'&&open=='(') d--;
                                else if(*p=='{'&&open=='{') d++;
                                else if(*p=='}'&&open=='{') d--;
                                if(d) p++;
                            }
                            if(*p) p++;
                            continue;
                        }
                        if(*p==' '||*p=='\t') break;
                    }
                    p++;
                }
                int len=p-start;
                char *w=malloc(len+1); memcpy(w,start,len); w[len]=0;
                words[nw++]=w;
            }
            words[nw]=NULL;
        }
        int wi=0; int bi=0;
        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(");
        while(wi<nw){
            if(!strcmp(words[wi],"&&")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"&&"); wi++; }
            else if(!strcmp(words[wi],"||")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"||"); wi++; }
            else if(!strcmp(words[wi],"!")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"!"); wi++; }
            else if(!strcmp(words[wi],"(")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"("); wi++; }
            else if(!strcmp(words[wi],")")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,")"); wi++; }
            else if(!strcmp(words[wi],"[")||!strcmp(words[wi],"]")){ wi++; /* skip brackets */ }
            /* Skip stderr redirects: 2>/dev/null, 2>&1, 1>/dev/null, etc. */
            else if(!strncmp(words[wi],"2>",2)||!strncmp(words[wi],"1>",2)||
                     !strcmp(words[wi],">/dev/null")||!strcmp(words[wi],"2>&1")||
                     !strcmp(words[wi],"2>&-")){ wi++; }
            /* Also skip bare redirect tokens: 2 > /dev/null (tokenized separately) */
            else if((!strcmp(words[wi],"2")||!strcmp(words[wi],"1"))&&
                     wi+1<nw&&!strcmp(words[wi+1],">")&&
                     wi+2<nw&&strstr(words[wi+2],"/dev/null")){
                wi+=3; /* skip "2" ">" "/dev/null" */
            }
            else if(!strcmp(words[wi],">")&&wi+1<nw&&
                     (strstr(words[wi+1],"/dev/null")||!strcmp(words[wi+1],"&1"))){
                wi+=2; /* skip ">" "/dev/null" or ">" "&1" */
            }
            else {
                if(wi+2<nw && (
                    !strcmp(words[wi+1],"=")||!strcmp(words[wi+1],"==")||
                    !strcmp(words[wi+1],"!=")||!strcmp(words[wi+1],"<=")||
                    !strcmp(words[wi+1],">=")||!strcmp(words[wi+1],"<")||
                    !strcmp(words[wi+1],">")||!strcmp(words[wi+1],"=~")||
                    !strcmp(words[wi+1],"\\<")||!strcmp(words[wi+1],"\\>")||
                    !strcmp(words[wi+1],"-eq")||!strcmp(words[wi+1],"-ne")||
                    !strcmp(words[wi+1],"-lt")||!strcmp(words[wi+1],"-le")||
                    !strcmp(words[wi+1],"-gt")||!strcmp(words[wi+1],"-ge")||
                    !strcmp(words[wi+1],"-ef")||!strcmp(words[wi+1],"-nt")||
                    !strcmp(words[wi+1],"-ot"))){
                    char *r=translate_test_binary(words[wi+1],words[wi],words[wi+2]);
                    bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                    wi+=3;
                } else if(wi+1<nw && (
                    !strcmp(words[wi],"-z")||!strcmp(words[wi],"-n")||
                    !strcmp(words[wi],"-f")||!strcmp(words[wi],"-d")||
                    !strcmp(words[wi],"-e")||!strcmp(words[wi],"-r")||
                    !strcmp(words[wi],"-w")||!strcmp(words[wi],"-x")||
                    !strcmp(words[wi],"-s")||!strcmp(words[wi],"-h")||
                    !strcmp(words[wi],"-L")||!strcmp(words[wi],"-p")||
                    !strcmp(words[wi],"-S")||!strcmp(words[wi],"-b")||
                    !strcmp(words[wi],"-c")||!strcmp(words[wi],"-t")||
                    !strcmp(words[wi],"-v")||!strcmp(words[wi],"-O")||
                    !strcmp(words[wi],"-G")||!strcmp(words[wi],"-N"))){
                    char *r=translate_test_unary(words[wi],words[wi+1]);
                    bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                    wi+=2;
                } else {
                    char *r=translate_operand(words[wi]);
                    bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                    free(r);
                    wi++;
                }
            }
        }
        bi+=snprintf(buf+bi,sizeof(buf)-bi,")");
        buf[bi]=0;
        return buf;
        } /* end if(has_close) */
    } /* end if([ ) */
    /* [[ ... ]] — strip the brackets if present */
    if(starts_with(s,"[[")){
        s+=2; while(isspace((unsigned char)*s)) s++;
        char tmp[2048]; strncpy(tmp,s,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
        int rp=(int)strlen(tmp);
        /* strip trailing whitespace */
        while(rp>0 && isspace((unsigned char)tmp[rp-1])) tmp[--rp]=0;
        /* strip trailing ]] (exactly 2 brackets) */
        if(rp>=2 && tmp[rp-1]==']' && tmp[rp-2]==']'){ tmp[--rp]=0; tmp[--rp]=0; }
        else if(rp>=1 && tmp[rp-1]==']'){ tmp[--rp]=0; } /* single ] */
        s=tmp;
        /* tokenize on spaces, handle && || ! */
        /* Custom tokenizer that protects $((...)), $(...), ${...}, and quotes */
        char *words[256]; int nw=0;
        {
            char *cp=strdup(s);
            const char *p=cp;
            while(*p && nw<255){
                while(*p==' '||*p=='\t') p++;
                if(!*p) break;
                const char *start=p;
                int dq=0,sq=0;
                while(*p){
                    if(*p=='"'&&!sq) dq=!dq;
                    else if(*p=='\''&&!dq) sq=!sq;
                    else if(!dq&&!sq){
                        if(*p=='$'&&(*(p+1)=='('||*(p+1)=='{')){
                            /* skip $(...), $((...)), ${...} */
                            char open=*++p; int d=1; p++;
                            while(*p&&d){
                                if(*p=='('&&open=='(') d++;
                                else if(*p==')'&&open=='(') d--;
                                else if(*p=='{'&&open=='{') d++;
                                else if(*p=='}'&&open=='{') d--;
                                if(d) p++;
                            }
                            if(*p) p++;
                            continue;
                        }
                        if(*p==' '||*p=='\t') break;
                    }
                    p++;
                }
                int len=p-start;
                char *w=malloc(len+1); memcpy(w,start,len); w[len]=0;
                words[nw++]=w;
            }
            words[nw]=NULL;
        }
        int wi=0; int bi=0;
        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(");
        while(wi<nw){
            if(!strcmp(words[wi],"&&")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"&&"); wi++; }
            else if(!strcmp(words[wi],"||")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"||"); wi++; }
            else if(!strcmp(words[wi],"!")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"!"); wi++; }
            else if(!strcmp(words[wi],"(")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"("); wi++; }
            else if(!strcmp(words[wi],")")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,")"); wi++; }
            else if(!strcmp(words[wi],"[")||!strcmp(words[wi],"]")){ wi++; /* skip brackets */ }
            else {
                /* look ahead for binary op */
                if(wi+2<nw && (
                    !strcmp(words[wi+1],"=")||!strcmp(words[wi+1],"==")||
                    !strcmp(words[wi+1],"!=")||!strcmp(words[wi+1],"<")||
                    !strcmp(words[wi+1],">")||!strcmp(words[wi+1],"=~")||
                    !strcmp(words[wi+1],"\\<")||!strcmp(words[wi+1],"\\>")||
                    !strcmp(words[wi+1],"-eq")||!strcmp(words[wi+1],"-ne")||
                    !strcmp(words[wi+1],"-lt")||!strcmp(words[wi+1],"-le")||
                    !strcmp(words[wi+1],"-gt")||!strcmp(words[wi+1],"-ge")||
                    !strcmp(words[wi+1],"-ef")||!strcmp(words[wi+1],"-nt")||
                    !strcmp(words[wi+1],"-ot"))){
                    if(!strcmp(words[wi+1],"=~")){
                        /* regex pattern: merge remaining words into one pattern
                         * (tokenizer may have split [a-z] etc.)
                         * Join without spaces for regex patterns */
                        char patbuf[512]; int pi=0;
                        char *lhs=translate_operand(words[wi]);
                        /* Start from words[wi+2], merge until end */
                        for(int j=wi+2; j<nw && pi<(int)sizeof(patbuf)-4; j++){
                            for(const char *c=words[j]; *c && pi<(int)sizeof(patbuf)-4; c++){
                                if(*c=='"'||*c=='\\') patbuf[pi++]='\\';
                                patbuf[pi++]=*c;
                            }
                        }
                        patbuf[pi]=0;
                        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(__sh_regex(%s,\"%s\"))",lhs,patbuf);
                        free(lhs);
                        wi=nw; /* consume rest */
                        continue;
                    } else if(!strcmp(words[wi+1],"==")||!strcmp(words[wi+1],"!=")){
                        /* check if right side is a glob pattern (unquoted with * ? [) */
                        const char *rhs=words[wi+2];
                        int is_glob = (rhs[0]!='"'&&rhs[0]!='\'') && (strchr(rhs,'*')||strchr(rhs,'?')||strchr(rhs,'['));
                        if(is_glob){
                            char *lhs=translate_operand(words[wi]);
                            /* strip quotes from rhs for pattern */
                            char pat[256]; strncpy(pat,rhs,sizeof(pat)-1); pat[sizeof(pat)-1]=0;
                            int pl=(int)strlen(pat);
                            if(pl>=2&&pat[0]=='"'&&pat[pl-1]=='"'){ memmove(pat,pat+1,pl-2); pat[pl-2]=0; }
                            if(!strcmp(words[wi+1],"=="))
                                bi+=snprintf(buf+bi,sizeof(buf)-bi,"(fnmatch(\"%s\",%s,0)==0)",pat,lhs);
                            else
                                bi+=snprintf(buf+bi,sizeof(buf)-bi,"(fnmatch(\"%s\",%s,0)!=0)",pat,lhs);
                            free(lhs);
                        } else {
                            char *r=translate_test_binary(words[wi+1],words[wi],words[wi+2]);
                            bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                        }
                    } else {
                        char *r=translate_test_binary(words[wi+1],words[wi],words[wi+2]);
                        bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                    }
                    wi+=3;
                } else if(wi+1<nw && (
                    !strcmp(words[wi],"-z")||!strcmp(words[wi],"-n")||
                    !strcmp(words[wi],"-f")||!strcmp(words[wi],"-d")||
                    !strcmp(words[wi],"-e")||!strcmp(words[wi],"-r")||
                    !strcmp(words[wi],"-w")||!strcmp(words[wi],"-x")||
                    !strcmp(words[wi],"-s")||!strcmp(words[wi],"-h")||
                    !strcmp(words[wi],"-L")||!strcmp(words[wi],"-p")||
                    !strcmp(words[wi],"-S")||!strcmp(words[wi],"-b")||
                    !strcmp(words[wi],"-c")||!strcmp(words[wi],"-t")||
                    !strcmp(words[wi],"-v")||!strcmp(words[wi],"-O")||
                    !strcmp(words[wi],"-G")||!strcmp(words[wi],"-N"))){
                    char *r=translate_test_unary(words[wi],words[wi+1]);
                    bi+=snprintf(buf+bi,sizeof(buf)-bi,"%s",r);
                    wi+=2;
                } else {
                    /* Check if this is a user-defined function call */
                    if(is_user_func(words[wi])){
                        /* merge remaining words as function args */
                        char fargs[1024]; int fai=0;
                        fai+=snprintf(fargs+fai,sizeof(fargs)-fai,"__exit_status=0; int __save_argc=__sh_argc; char *__av[]={");
                        int fnargs=0;
                        int j;
                        for(j=wi+1; j<nw; j++){
                            /* stop at && || ! */
                            if(!strcmp(words[j],"&&")||!strcmp(words[j],"||")||!strcmp(words[j],"!")) break;
                            /* Use av_arg_expr: guarantees char* type for __av[] */
                            char *e=av_arg_expr(words[j]);
                            fai+=snprintf(fargs+fai,sizeof(fargs)-fai,"%s,",e);
                            free(e);
                            fnargs++;
                        }
                        fai+=snprintf(fargs+fai,sizeof(fargs)-fai,"NULL}; __sh_argc=%d; %s(__sh_argc,__av); __sh_argc=__save_argc; (__exit_status==0);",
                            fnargs,safe_cname(words[wi]));
                        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(int)({%s})",fargs);
                        wi=j;
                    } else {
                        /* bare word — truthiness */
                        char *e=translate_expr(words[wi]);
                        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(%s[0]!='\\0')",e);
                        free(e);
                        wi++;
                    }
                }
            }
        }
        bi+=snprintf(buf+bi,sizeof(buf)-bi,")");
        buf[bi]=0; /* ensure null termination */
        return buf;
    }
    /* [ ... ] */
    if(*s=='['){        s++;
        while(isspace((unsigned char)*s)) s++;
        char tmp[2048]; strncpy(tmp,s,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
        int rp=(int)strlen(tmp)-1;
        while(rp>=0 && (isspace((unsigned char)tmp[rp])||tmp[rp]==']')) tmp[rp--]=0;
        s=tmp;
        /* Custom token splitter that handles $((...)), $(...), ${...}, quotes */
        char tok1[256]="",op[32]="",tok2[256]="";
        int nt3=0;
        const char *p=s;
        while(*p && nt3<3){
            while(*p==' '||*p=='\t') p++;
            if(!*p) break;
            char *buf; int bufsz;
            if(nt3==0){ buf=tok1; bufsz=256; }
            else if(nt3==1){ buf=op; bufsz=32; }
            else { buf=tok2; bufsz=256; }
            int bi=0;
            while(*p && *p!=' ' && *p!='\t' && bi<bufsz-1){
                if(*p=='"'||*p=='\''){
                    char qc=*p; buf[bi++]=*p++;
                    while(*p && *p!=qc && bi<bufsz-1) buf[bi++]=*p++;
                    if(*p==qc) buf[bi++]=*p++;
                } else if(*p=='$' && *(p+1)=='('){
                    buf[bi++]='$'; buf[bi++]='('; p+=2;
                    int d=1;
                    while(*p && d && bi<bufsz-1){
                        if(*p=='(') d++;
                        else if(*p==')') d--;
                        buf[bi++]=*p++;
                    }
                } else if(*p=='$' && *(p+1)=='{'){
                    buf[bi++]='$'; buf[bi++]='{'; p+=2;
                    while(*p && *p!='}' && bi<bufsz-1) buf[bi++]=*p++;
                    if(*p=='}') buf[bi++]=*p++;
                } else {
                    buf[bi++]=*p++;
                }
            }
            buf[bi]=0;
            if(bi>0) nt3++;
        }
        static const char *unary[]={"-z","-n","-f","-d","-e","-r","-w","-x","-s","-h","-L","-p","-S","-b","-c","-t","-g","-u","-k","-v","-O","-G","-N",NULL};
        /* Handle [ ! ... ] — negation */
        if(!strcmp(tok1,"!") && op[0]){
            /* shift: tok1=op, op=tok2, tok2="" */
            char t[256]; snprintf(t,sizeof(t),"%s",tok1);
            snprintf(tok1,sizeof(tok1),"%s",op);
            snprintf(op,sizeof(op),"%s",tok2);
            tok2[0]=0;
            /* re-check unary */
            int is_u2=0;
            for(int i=0;unary[i];i++) if(strcmp(tok1,unary[i])==0){is_u2=1;break;}
            if(is_u2){
                char tmp[256]; snprintf(tmp,sizeof(tmp),"%s",op);
                snprintf(op,sizeof(op),"%s",tok1);
                snprintf(tok1,sizeof(tok1),"%s",tmp); tok2[0]=0;
            }
            if(op[0]){
                char *r=translate_test_unary(op,tok1);
                snprintf(buf,sizeof(buf),"(!%s)",r);
                return buf;
            }
            /* bare [ ! string ] */
            char *e=translate_expr(tok1);
            snprintf(buf,sizeof(buf),"(!(%s[0]!='\\0'))",e);
            free(e);
            return buf;
        }
        int is_u=0;
        for(int i=0;unary[i];i++) if(strcmp(tok1,unary[i])==0){is_u=1;break;}
        if(is_u){
            char t[256]; strncpy(t,op,255); t[255]=0;
            strncpy(op,tok1,31); op[31]=0;
            strncpy(tok1,t,255); tok1[255]=0; tok2[0]=0;
        }
        if(!strcmp(op,"!") && tok2[0]){
            /* [ ! -z x ] etc. */
            char *r=translate_test_unary(tok2,tok1);
            snprintf(buf,sizeof(buf),"(!%s)",r);
            return buf;
        }
        if(tok2[0]){
            char *r=translate_test_binary(op,tok1,tok2);
            snprintf(buf,sizeof(buf),"%s",r);
            return buf;
        }
        if(op[0]){
            char *r=translate_test_unary(op,tok1);
            snprintf(buf,sizeof(buf),"%s",r);
            return buf;
        }
        /* bare [ string ] */
        char *e=translate_expr(tok1);
        snprintf(buf,sizeof(buf),"(%s[0]!='\\0')",e);
        free(e);
        return buf;
    }
    /* $((expr)) as condition */
    if(starts_with(s,"$((")){
        char *e=translate_expr(s);
        snprintf(buf,sizeof(buf),"(%s)",e); free(e); return buf;
    }
    /* ((expr)) as condition — arithmetic test */
    if(starts_with(s,"((")){
        char inner[2048]; strncpy(inner,s+2,sizeof(inner)-1); inner[sizeof(inner)-1]=0;
        int l=(int)strlen(inner);
        if(l>=2 && inner[l-1]==')' && inner[l-2]==')') inner[l-2]=0;
        char arith[2100]; snprintf(arith,sizeof(arith),"$((%s))",inner);
        char *e=translate_expr(arith);
        snprintf(buf,sizeof(buf),"((%s)!=0)",e); free(e); return buf;
    }
    /* true / false as condition */
    if(!strcmp(s,"true")||!strcmp(s,":")) return (char*)"1";
    if(!strcmp(s,"false")) return (char*)"0";
    /* user-defined function as condition (with optional ! prefix) */
    {
        /* skip leading ! */
        const char *fs=s;
        int negate=0;
        while(*fs=='!'){ negate=!negate; fs++; while(*fs==' '||*fs=='\t') fs++; }
        /* extract first word (function name) */
        char fname[128]; int fi=0;
        const char *p=fs;
        while(*p && *p!=' ' && *p!='\t' && fi<(int)sizeof(fname)-1) fname[fi++]=*p++;
        fname[fi]=0;
        if(fi>0 && is_user_func(fname)){
            /* call the function and check __exit_status */
            char args[2048]; int ai=0; int nargs=0;
            ai+=snprintf(args+ai,sizeof(args)-ai,"__exit_status=0; char *__av[]={");
            /* build argv */
            /* skip function name */
            p=fs;
            while(*p && *p!=' ' && *p!='\t') p++;
            while(*p){
                while(*p==' '||*p=='\t') p++;
                if(!*p) break;
                char arg[256]; int al=0;
                if(*p=='"'){ p++; while(*p && *p!='"' && al<(int)sizeof(arg)-1) arg[al++]=*p++; if(*p=='"')p++; }
                else if(*p=='\''){ p++; while(*p && *p!='\'' && al<(int)sizeof(arg)-1) arg[al++]=*p++; if(*p=='\'')p++; }
                else { while(*p && *p!=' ' && *p!='\t' && al<(int)sizeof(arg)-1) arg[al++]=*p++; }
                arg[al]=0;
                { char *e=av_arg_expr(arg);
                  ai+=snprintf(args+ai,sizeof(args)-ai,"%s,",e);
                  free(e);
                }
                nargs++;
            }
            ai+=snprintf(args+ai,sizeof(args)-ai,"NULL}; int __save_argc=__sh_argc; __sh_argc=%d; %s(__sh_argc,__av); __sh_argc=__save_argc; (__exit_status==0);",nargs,safe_cname(fname));
            if(negate)
                snprintf(buf,sizeof(buf),"! (int)({%s})",args);
            else
                snprintf(buf,sizeof(buf),"(int)({%s})",args);
            return buf;
        }
    }
    /* command as condition — non-empty output = true */
    {
        char esc[2048]; int e=0;
        for(const char *q=s;*q&&e<(int)sizeof(esc)-4;q++){
            if(*q=='"'||*q=='\\') esc[e++]='\\';
            esc[e++]=*q;
        }
        esc[e]=0;
        snprintf(buf,sizeof(buf),"(__sh_test_cmd(\"%s\"))",esc);
        return buf;
    }
}

