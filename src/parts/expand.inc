/* ================================================================== */
/* L6  String expander (printf-style format synthesis)                */
/* ================================================================== */
/* ExpandResult and EXPAND_MAX_ARGS defined in L5 section above */

void expand_free(ExpandResult *er){
    free(er->fmt);
    for(int i=0;i<er->nargs;i++) free(er->args[i]);
}

void expand_string(const char *s, ExpandResult *er){
    memset(er,0,sizeof(*er));
    char fmt[8192]; int fi=0;
    const char *p=s;
    /* strip outer quotes */
    char stripped[4096];
    if(p[0]=='"'){
        strncpy(stripped,p+1,sizeof(stripped)-1); stripped[sizeof(stripped)-1]=0;
        int l=(int)strlen(stripped);
        if(l>0&&stripped[l-1]=='"') stripped[--l]=0;
        p=stripped;
    } else if(p[0]=='\''){
        strncpy(stripped,p+1,sizeof(stripped)-1); stripped[sizeof(stripped)-1]=0;
        int l=(int)strlen(stripped);
        if(l>0&&stripped[l-1]=='\'') stripped[--l]=0;
        p=stripped;
    }

    while(*p && fi<(int)sizeof(fmt)-8){
        if(*p!='$' && *p!='\\'){
            if(*p=='%'){ fmt[fi++]='%'; fmt[fi++]='%'; }
            else fmt[fi++]=*p;
            p++; continue;
        }
        if(*p=='\\'){
            p++;
            switch(*p){
                case 'n': fmt[fi++]='\n'; break;
                case 't': fmt[fi++]='\t'; break;
                case 'r': fmt[fi++]='\r'; break;
                case '\\': fmt[fi++]='\\'; break;
                case '"': fmt[fi++]='"'; break;
                case '\'': fmt[fi++]='\''; break;
                case '0': fmt[fi++]='\0'; break;
                case '$': fmt[fi++]='$'; break;
                case '`': fmt[fi++]='`'; break;
                default:  if(*p) fmt[fi++]=*p; break;
            }
            if(*p) p++;
            continue;
        }
        /* $... */
        p++;
        if(*p=='('){
            if(*(p+1)=='('){
                /* $((expr)) — translate to a C arithmetic expression.
                 * Inside, $var / ${var} become the C variable (atoi'd if str),
                 * $1..$9 become atoi(__sh_argN), $? $# $$ $! become int syms. */
                p+=2;
                char expr[1024]; int k=0;
                int d=0;
                while(*p){
                    if(*p=='(') d++;
                    else if(*p==')'){ if(d==0 && *(p+1)==')') { p+=2; break; } d--; }
                    if(*p=='$'){
                        p++;
                        /* Check for $(cmd...) command substitution inside $(( )) */
                        if(*p=='(' && *(p+1)!='('){
                            p++; /* skip ( */
                            char cmd[1024]; int ci=0; int d=1;
                            while(*p && d && ci<(int)sizeof(cmd)-1){
                                if(*p=='(') d++;
                                else if(*p==')'){ d--; if(d==0) break; }
                                cmd[ci++]=*p++;
                            }
                            cmd[ci]=0; if(*p==')') p++;
                            /* Extract function name */
                            char fname[256]; int fi2=0;
                            const char *sp=cmd;
                            while(*sp==' '||*sp=='\t') sp++;
                            while(*sp && *sp!=' ' && *sp!='\t' && fi2<(int)sizeof(fname)-1) fname[fi2++]=*sp++;
                            fname[fi2]=0;
                            if(is_user_func(fname)){
                                const char *pre="atoi(__sh_capture_fn((void(*)(int,char**))";
                                while(*pre && k<(int)sizeof(expr)-32) expr[k++]=*pre++;
                                const char *cn=safe_cname(fname);
                                while(*cn && k<(int)sizeof(expr)-32) expr[k++]=*cn++;
                                /* Collect args into a temp buffer, then emit with proper format */
                                char argbuf[2048]; int abi=0;
                                const char *ap=cmd+fi2;
                                while(*ap==' '||*ap=='\t') ap++;
                                int nargs=0;
                                argbuf[abi]=0;
                                while(*ap && nargs<9){
                                    while(*ap==' '||*ap=='\t') ap++;
                                    if(!*ap) break;
                                    char arg[256]; int al=0;
                                    if(*ap=='$' && *(ap+1)=='('){
                                        /* $(...) or $((...)) — extract as one unit */
                                        ap+=2; int paren=1; int is_arith=0;
                                        if(*ap=='('){ is_arith=1; paren=2; ap++; }
                                        while(*ap && paren>0 && al<255){
                                            if(*ap=='(') { paren++; arg[al++]=*ap++; }
                                            else if(*ap==')'){ paren--; if(paren<=is_arith){ ap++; break; } arg[al++]=*ap++; }
                                            else arg[al++]=*ap++;
                                        }
                                        arg[al]=0;
                                        /* Translate $((expr)) to C expression */
                                        if(is_arith && al>0){
                                            char *t=translate_arith(arg);
                                            if(t){
                                                if(nargs>0) argbuf[abi++]=',';
                                                const char *pre="__sh_fmt(\"%d\",";
                                                while(*pre && abi<(int)sizeof(argbuf)-32) argbuf[abi++]=*pre++;
                                                for(int ai=0;t[ai]&&abi<(int)sizeof(argbuf)-8;ai++){
                                                    if(t[ai]=='"') argbuf[abi++]='\\';
                                                    argbuf[abi++]=t[ai];
                                                }
                                                argbuf[abi++]=')'; nargs++;
                                                free(t);
                                            }
                                            while(*ap==')') ap++;
                                            continue;
                                        } else {
                                            /* $(cmd) - pass as string */
                                            char *t=translate_expr(arg);
                                            if(nargs>0) argbuf[abi++]=',';
                                            argbuf[abi++]='"';
                                            for(int ai=0;arg[ai]&&abi<(int)sizeof(argbuf)-8;ai++){
                                                if(arg[ai]=='"') argbuf[abi++]='\\';
                                                argbuf[abi++]=arg[ai];
                                            }
                                            argbuf[abi++]='"'; nargs++;
                                            if(t) free(t);
                                        }
                                        continue;
                                    } else if(*ap=='"'){ ap++; while(*ap&&*ap!='"'&&al<255) arg[al++]=*ap++; if(*ap=='"')ap++; }
                                    else { while(*ap&&*ap!=' '&&*ap!='\t'&&al<255) arg[al++]=*ap++; }
                                    arg[al]=0;
                                    if(al>0){
                                        if(nargs>0) argbuf[abi++]=',';
                                        argbuf[abi++]='"';
                                        for(int ai=0;ai<al&&abi<(int)sizeof(argbuf)-8;ai++) argbuf[abi++]=arg[ai];
                                        argbuf[abi++]='"'; nargs++;
                                    }
                                }
                                argbuf[abi]=0;
                                /* Emit: ,N,(char*[]){argbuf,NULL}) ) */
                                k+=snprintf(expr+k,sizeof(expr)-k,",%d,(char*[]){%s,NULL}))",nargs,argbuf);
                            } else {
                                /* External command → atoi(__sh_cmd_output("cmd")) */
                                const char *pre2="atoi(__sh_cmd_output(\"";
                                while(*pre2 && k<(int)sizeof(expr)-32) expr[k++]=*pre2++;
                                for(int ei=0;cmd[ei]&&k<(int)sizeof(expr)-8;ei++){
                                    if(cmd[ei]=='"'||cmd[ei]=='\\') expr[k++]='\\';
                                    expr[k++]=cmd[ei];
                                }
                                const char *suf="\"))";
                                while(*suf && k<(int)sizeof(expr)-8) expr[k++]=*suf++;
                            }
                        } else if(*p=='{'){
                            p++;
                            char nm[128]; int j=0;
                            while(*p&&*p!='}') nm[j++]=*p++;
                            nm[j]=0; if(*p) p++;
                            if(j>0){
                                /* check for ${#arr[@]} — array count */
                                if(nm[0]=='#'){
                                    char arrn[128]; int ai=0; const char *ap=nm+1;
                                    while(*ap && *ap!='[' && ai<(int)sizeof(arrn)-1) arrn[ai++]=*ap++;
                                    arrn[ai]=0;
                                    if(*ap=='[' && (!strcmp(ap,"[@]")||!strcmp(ap,"[*]"))){
                                        const char *s="__sh_arr_count(__arr_"; while(*s)expr[k++]=*s++;
                                        const char *cn=safe_cname(arrn); while(*cn)expr[k++]=*cn++;
                                        expr[k++]=')';
                                    } else {
                                        /* ${#var} — string length */
                                        VarKind vk=get_var_kind(nm+1);
                                        if(vk==V_INT){
                                            const char *s="(int)snprintf(NULL,0,\"%d\","; while(*s)expr[k++]=*s++;
                                            const char *cn=safe_cname(nm+1); while(*cn)expr[k++]=*cn++;
                                            expr[k++]=')';
                                        } else {
                                            const char *s="(int)strlen("; while(*s)expr[k++]=*s++;
                                            const char *cn=safe_cname(nm+1); while(*cn)expr[k++]=*cn++;
                                            expr[k++]=')';
                                        }
                                    }
                                } else {
                                    VarKind vk=get_var_kind(nm);
                                    if(vk==V_STR){
                                        const char *pre="atoi("; while(*pre)expr[k++]=*pre++;
                                        const char *cn=safe_cname(nm); while(*cn)expr[k++]=*cn++;
                                        expr[k++]=')';
                                    } else {
                                        const char *cn=safe_cname(nm); while(*cn)expr[k++]=*cn++;
                                    }
                                }
                            }
                        } else if(isdigit((unsigned char)*p)){
                            int na=0; while(isdigit((unsigned char)*p)) na=na*10+(*p++)-'0';
                            if(na>=1&&na<=9){
                                const char *pre="atoi(__sh_arg"; while(*pre)expr[k++]=*pre++;
                                expr[k++]='0'+na; expr[k++]=')';
                            }
                        } else if(*p=='?'){
                            const char *s="__exit_status"; while(*s)expr[k++]=*s++; p++;
                        } else if(*p=='#'){
                            const char *s="__sh_argc"; while(*s)expr[k++]=*s++; p++;
                        } else if(*p=='$'){
                            const char *s="(int)getpid()"; while(*s)expr[k++]=*s++; p++;
                        } else if(*p=='!'){
                            const char *s="__sh_last_bg_pid"; while(*s)expr[k++]=*s++; p++;
                        } else {
                            char nm[128]; int j=0;
                            while(isalnum((unsigned char)*p)||*p=='_') nm[j++]=*p++;
                            nm[j]=0;
                            if(j>0){
                                VarKind vk=get_var_kind(nm);
                                if(vk==V_STR){
                                    const char *pre="atoi("; while(*pre)expr[k++]=*pre++;
                                    const char *cn=safe_cname(nm); while(*cn)expr[k++]=*cn++;
                                    expr[k++]=')';
                                } else {
                                    const char *cn=safe_cname(nm); while(*cn)expr[k++]=*cn++;
                                }
                            }
                        }
                    }
                    else if(isalpha((unsigned char)*p)||*p=='_'){
                        /* bare variable name — look up and translate */
                        char nm[128]; int j=0;
                        while(isalnum((unsigned char)*p)||*p=='_'){
                            if(j<(int)sizeof(nm)-1) nm[j++]=*p;
                            p++;
                        }
                        nm[j]=0;
                        VarKind vk=get_var_kind(nm);
                        const char *cn=safe_cname(nm);
                        /* check for = assignment (not ==) — register as int var */
                        {
                            const char *np=p; while(*np==' '||*np=='\t') np++;
                            if(*np=='='&&*(np+1)!='='){
                                add_var(nm,V_INT);
                                vk=V_INT; cn=safe_cname(nm);
                            }
                        }
                        /* handle ++ and -- for string variables */
                        if(vk==V_STR && (*p=='+'&&*(p+1)=='+')){
                            p+=2;
                            k+=snprintf(expr+k,sizeof(expr)-k,
                                "(atoi(%s),(snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)+1),0))",cn,cn,cn,cn);
                        } else if(vk==V_STR && (*p=='-'&&*(p+1)=='-')){
                            p+=2;
                            k+=snprintf(expr+k,sizeof(expr)-k,
                                "(atoi(%s),(snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)-1),0))",cn,cn,cn,cn);
                        } else if(vk==V_STR){
                            const char *pre="atoi("; while(*pre)expr[k++]=*pre++;
                            while(*cn)expr[k++]=*cn++;
                            expr[k++]=')';
                        } else {
                            while(*cn)expr[k++]=*cn++;
                        }
                    }
                    else if(*p=='*'&&*(p+1)=='*'){
                        /* ** → power operator, use __sh_pow */
                        /* skip trailing spaces in expr to find base */
                        int bs=k;
                        while(bs>0 && expr[bs-1]==' ') bs--;
                        while(bs>0 && (isalnum((unsigned char)expr[bs-1])||expr[bs-1]=='_')) bs--;
                        if(bs<k){
                            char base[256]; int bl=k-bs;
                            memcpy(base,expr+bs,bl); base[bl]=0;
                            k=bs;
                            const char *fn="(int)__sh_pow("; while(*fn) expr[k++]=*fn++;
                            for(int bi=0;base[bi];bi++) expr[k++]=base[bi];
                            expr[k++]=',';
                            p+=2;
                            while(*p==' ') p++;
                            while(*p && *p!=')' && *p!=' ' && *p!='+' && *p!='-' && *p!='*' && *p!='/' && *p!='%' && k<(int)sizeof(expr)-4){
                                expr[k++]=*p++;
                            }
                            expr[k++]=')';
                        } else {
                            expr[k++]='*'; p+=2;
                        }
                    }
                    else {
                        /* Detect /0 or %0 — emit runtime error + safe fallback */
                        if((*p=='/'||*p=='%')){
                            const char *np=p+1;
                            while(*np==' '||*np=='\t') np++;
                            if(*np=='0' && !isdigit((unsigned char)*(np+1)) && *(np+1)!='x' && *(np+1)!='X'){
                                /* Build expression string for error message */
                                char div_expr[64]; int di2=0;
                                int bs2=k; while(bs2>0 && (isdigit((unsigned char)expr[bs2-1])||expr[bs2-1]==' ')) bs2--;
                                int be2=bs2; while(be2<k) div_expr[di2++]=expr[be2++];
                                div_expr[di2++]=' '; div_expr[di2++]=*p; div_expr[di2++]=' '; div_expr[di2++]='0'; div_expr[di2]=0;
                                k=bs2;
                                k+=snprintf(expr+k,sizeof(expr)-k,"(__sh_div_zero(\"%s\"),1)",div_expr);
                                p=np+1; continue;
                            }
                        }
                        expr[k++]=*p++;
                    }
                    if(k>=(int)sizeof(expr)-2) break;
                }
                expr[k]=0;
                fmt[fi++]='%'; fmt[fi++]='d';
                if(er->nargs<EXPAND_MAX_ARGS){
                    er->args[er->nargs]=xstrdup(expr);
                    er->arg_is_int[er->nargs++]=1;
                }
            } else {
                /* $(cmd) command substitution */
                p++;
                char cmd[1024]; int k=0; int dd=1;
                while(*p && dd){ if(*p=='(')dd++; else if(*p==')'){dd--; if(dd==0)break;} cmd[k++]=*p++; }
                if(*p==')') p++;
                cmd[k]=0;
                fmt[fi++]='%'; fmt[fi++]='s';
                char r[4096];
                /* Check if the command is a user-defined function */
                char fname[256]; int fi2=0;
                const char *sp=cmd;
                while(*sp==' '||*sp=='\t') sp++;
                while(*sp && *sp!=' ' && *sp!='\t' && fi2<(int)sizeof(fname)-1) fname[fi2++]=*sp++;
                fname[fi2]=0;
                if(is_user_func(fname)){
                    char args[2048]; int ai=0; int nargs=0;
                    ai += snprintf(args,sizeof(args),"(char*[]){");
                    while(*sp){
                        while(*sp==' '||*sp=='\t') sp++;
                        if(!*sp) break;
                        char arg[512]; int ar=0;
                        if(*sp=='"'){ sp++; while(*sp&&*sp!='"'&&ar<(int)sizeof(arg)-1){ if(*sp=='\\'&&*(sp+1))sp++; arg[ar++]=*sp++; } if(*sp=='"')sp++; }
                        else if(*sp=='\''){ sp++; while(*sp&&*sp!='\''&&ar<(int)sizeof(arg)-1) arg[ar++]=*sp++; if(*sp=='\'')sp++; }
                        else if(*sp=='$' && *(sp+1)=='('){
                            /* $(...) or $((...)) — read as single unit */
                            arg[ar++]='$'; sp++;
                            arg[ar++]='('; sp++;
                            int d=1;
                            while(*sp && d && ar<(int)sizeof(arg)-1){
                                if(*sp=='(') d++;
                                else if(*sp==')') d--;
                                arg[ar++]=*sp++;
                            }
                        }
                        else { while(*sp&&*sp!=' '&&*sp!='\t'&&ar<(int)sizeof(arg)-1) arg[ar++]=*sp++; }
                        arg[ar]=0;
                        /* translate the argument */
                        if(arg[0]=='$' && arg[1]=='(' && arg[2]=='('){
                            /* $((expr)) — int result, convert to string via __sh_fmt */
                            char *e=translate_expr(arg);
                            ai += snprintf(args+ai,sizeof(args)-ai,"__sh_fmt(\"%%d\",(int)(%s)),",e);
                            free(e);
                        } else if(arg[0]=='$' && arg[1]=='('){
                            /* $(cmd) — string result */
                            char *e=translate_expr(arg);
                            ai += snprintf(args+ai,sizeof(args)-ai,"%s,",e);
                            free(e);
                        } else if(arg[0]=='$'){
                            /* $var — string variable */
                            char *e=translate_expr(arg);
                            ai += snprintf(args+ai,sizeof(args)-ai,"%s,",e);
                            free(e);
                        } else {
                            ai += snprintf(args+ai,sizeof(args)-ai,"\"%s\",",arg);
                        }
                        nargs++;
                    }
                    ai += snprintf(args+ai,sizeof(args)-ai,"NULL}");
                    snprintf(r,sizeof(r),"__sh_capture_fn((void(*)(int,char**))%s,%d,%s)",
                             safe_cname(fname),nargs,args);
                } else {
                    /* Check for [[ ... ]] — bash extended test */
                    if(cmd[0]=='[' && cmd[1]=='[' && cmd[2]==' '){
                        /* Extract condition between [[ and ]] */
                        const char *cs=cmd+3;
                        int clen=(int)strlen(cs);
                        /* Remove trailing ]] */
                        while(clen>0 && (cs[clen-1]==' '||cs[clen-1]==']')) clen--;
                        /* Find end of condition (before ]] at end) */
                        char cond[1024]; int ci2=0;
                        for(int i=0;i<clen && ci2<(int)sizeof(cond)-1;i++){
                            cond[ci2++]=cs[i];
                        }
                        cond[ci2]=0;
                        /* Translate [[ cond ]] → __sh_test_ext("cond") */
                        snprintf(r,sizeof(r),"__sh_test_ext(\"%s\")",cond);
                    } else {
                        char *expanded=expand_cmd_subst(cmd);
                        if(expanded){
                            snprintf(r,sizeof(r),"__sh_cmd_output(%s)",expanded);
                        } else {
                            char esc[1050]; int e=0;
                            for(int i=0;cmd[i]&&e<(int)sizeof(esc)-6;i++){
                                if(cmd[i]=='"'||cmd[i]=='\\') esc[e++]='\\';
                                if(cmd[i]=='\n'){ esc[e++]='\\'; esc[e++]='n'; continue; }
                                if(cmd[i]=='\t'){ esc[e++]='\\'; esc[e++]='t'; continue; }
                                esc[e++]=cmd[i];
                            }
                            esc[e]=0;
                            snprintf(r,sizeof(r),"__sh_cmd_output(\"%s\")",esc);
                        }
                    }
                }
                if(er->nargs<EXPAND_MAX_ARGS){
                    er->args[er->nargs]=xstrdup(r);
                    er->arg_is_int[er->nargs++]=0;
                }
            }
            continue;
        }
        if(*p=='{'){
            p++;
            char body[256]; int k=0;
            /* Read body with nested brace support */
            int brace_depth=1;
            while(*p && brace_depth>0 && k<255){
                if(*p=='{') brace_depth++;
                else if(*p=='}'){ brace_depth--; if(brace_depth==0) break; }
                body[k++]=*p++;
            }
            body[k]=0; if(*p=='}') p++;
            char *e2=translate_brace_expansion(body);
            /* decide int vs str: length expressions are int, others str */
            int is_int = (body[0]=='#') || (strncmp(body,"${#",3)==0);
            VarKind vk = V_STR;
            /* peek name */
            char nm[128]; int j=0; const char *pp=body;
            while(*pp && *pp!='}'&&*pp!='['&&*pp!=':'&&*pp!='/'&&*pp!='%'&&*pp!='#'&&*pp!='^'&&*pp!=',') nm[j++]=*pp++;
            nm[j]=0;
            if(body[0]=='#') is_int=1;
            else if(*pp=='\0'||*pp=='}') vk=get_var_kind(nm);
            fmt[fi++]='%'; fmt[fi++]=(is_int||(vk==V_INT))?'d':'s';
            if(er->nargs<EXPAND_MAX_ARGS){
                /* for plain ${var} of unknown var, use getenv */
                if(*pp=='\0'||*pp=='}'){
                    if(!is_known_var(nm) && body[0]!='#'){
                        char buf[300]; snprintf(buf,sizeof(buf),"(__sh_getenv(\"%s\"))",nm);
                        er->args[er->nargs]=xstrdup(buf);
                    } else
                        er->args[er->nargs]=e2;
                } else
                    er->args[er->nargs]=e2;
                er->arg_is_int[er->nargs++]=(is_int||(vk==V_INT));
            } else free(e2);
            continue;
        }
        if(*p=='?'){ fmt[fi++]='%'; fmt[fi++]='d';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("__exit_status"); er->arg_is_int[er->nargs++]=1; }
            p++; continue; }
        if(*p=='#'){ fmt[fi++]='%'; fmt[fi++]='d';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("__sh_argc"); er->arg_is_int[er->nargs++]=1; }
            p++; continue; }
        if(*p=='$'){ fmt[fi++]='%'; fmt[fi++]='d';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("(int)getpid()"); er->arg_is_int[er->nargs++]=1; }
            p++; continue; }
        if(*p=='!'){ fmt[fi++]='%'; fmt[fi++]='d';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("__sh_last_bg_pid"); er->arg_is_int[er->nargs++]=1; }
            p++; continue; }
        if(*p=='@'||*p=='*'){ fmt[fi++]='%'; fmt[fi++]='s';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("__sh_join_args(__sh_argc,__sh_args)"); er->arg_is_int[er->nargs++]=0; }
            p++; continue; }
        if(isdigit((unsigned char)*p)){
            char r[32]; snprintf(r,sizeof(r),"__sh_arg%c",*p); p++;
            fmt[fi++]='%'; fmt[fi++]='s';
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup(r); er->arg_is_int[er->nargs++]=0; }
            continue;
        }
        { char name[128]; int j=0;
          while(isalnum((unsigned char)*p)||*p=='_') name[j++]=*p++;
          name[j]=0;
          if(j>0){
              VarKind vk=get_var_kind(name);
              fmt[fi++]='%'; fmt[fi++]=(vk==V_INT)?'d':'s';
              if(er->nargs<EXPAND_MAX_ARGS){
                  er->args[er->nargs]=xstrdup(var_c_expr(name));
                  er->arg_is_int[er->nargs++]=(vk==V_INT);
              }
          } else { fmt[fi++]='$'; }
        }
    }
    fmt[fi]=0;
    er->fmt=xstrdup(fmt);
}

