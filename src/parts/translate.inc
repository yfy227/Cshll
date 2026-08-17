/* ================================================================== */
/* L5  Expression translator  ($var, ${...}, $((...)))                */
/* ================================================================== */

/* Forward declarations for expand_string (defined later in L6) */
/* ExpandResult defined in s2c_emit.h */
void expand_string(const char *s, ExpandResult *er);
void expand_free(ExpandResult *er);

char *translate_expr(const char *tok);

/* Translate ${...} body (without the braces) into a C expression string.
 * Returns heap string. */
char *translate_brace_expansion(const char *body){
    /* body is the content between { and } */
    char name[128]; int j=0;
    const char *p=body;
    /* ${#var} — length, or ${#arr[@]} — array count */
    if(*p=='#' && *(p+1)!='}'){
        p++;
        while(*p && *p!='}' && *p!='[' && *p!=':' && *p!='/' && *p!='%' && *p!='^' && *p!=',') name[j++]=*p++;
        name[j]=0;
        char r[512];
        /* ${#arr[@]} or ${#arr[*]} — array element count */
        if(*p=='['){
            p++; char idx[32]; int k=0;
            while(*p && *p!=']') idx[k++]=*p++;
            idx[k]=0; if(*p) p++;
            if(*p=='}') p++;
            if(!strcmp(idx,"@")||!strcmp(idx,"*")){
                snprintf(r,sizeof(r),"__sh_arr_count(__arr_%s)",safe_cname(name));
            } else {
                snprintf(r,sizeof(r),"1");
            }
            return xstrdup(r);
        }
        if(get_var_kind(name)==V_INT)
            snprintf(r,sizeof(r),"(int)snprintf(NULL,0,\"%%d\",%s)",safe_cname(name));
        else if(get_var_kind(name)==V_ARRAY)
            snprintf(r,sizeof(r),"__sh_arr_count(__arr_%s)",safe_cname(name));
        else
            snprintf(r,sizeof(r),"(int)strlen(%s)",safe_cname(name));
        return xstrdup(r);
    }
    /* ${!var} — indirect */
    if(*p=='!' && *(p+1)!='}'){
        p++;
        while(*p && *p!='}') name[j++]=*p++;
        name[j]=0;
        char r[512];
        if(!strcmp(name,"#")){
            /* ${!#} = last positional parameter */
            snprintf(r,sizeof(r),"__sh_indirect_args(__sh_argc,__sh_args)");
        } else
            snprintf(r,sizeof(r),"__sh_indirect(\"%s\")",name);
        return xstrdup(r);
    }
    /* read name up to operator */
    while(*p && *p!='}' && *p!='[' && *p!=':' && *p!='/' && *p!='%' && *p!='#' && *p!='^' && *p!=',') name[j++]=*p++;
    name[j]=0;
    /* array index */
    if(*p=='['){
        p++; char idx[64]; int k=0;
        while(*p && *p!=']') idx[k++]=*p++;
        idx[k]=0; if(*p) p++;
        char r[512];
        /* [@] or [*] — all elements */
        if(!strcmp(idx,"@")||!strcmp(idx,"*")){
            /* check for slice: ${arr[@]:offset:length} */
            if(*p==':'){
                p++; char off[32]; int oi=0;
                while(*p && *p!=':' && *p!='}') off[oi++]=*p++;
                off[oi]=0;
                char len[32]=""; 
                if(*p==':'){ p++; int li=0; while(*p&&*p!='}') len[li++]=*p++; len[li]=0; }
                if(*p=='}') p++;
                if(len[0])
                    snprintf(r,sizeof(r),"__sh_arr_slice(__arr_%s,%s,%s)",safe_cname(name),off,len);
                else
                    snprintf(r,sizeof(r),"__sh_arr_slice(__arr_%s,%s,-1)",safe_cname(name),off);
            } else {
                if(*p=='}') p++;
                snprintf(r,sizeof(r),"__sh_arr_join(__arr_%s)",safe_cname(name));
            }
        } else {
            if(*p=='}') p++;
            /* BUG-10 fix: for non-numeric indices (associative arrays),
             * use __sh_assoc_get helper function */
            int is_numeric=1;
            for(const char *q=idx;*q;q++){
                if(!isdigit((unsigned char)*q) && *q!='+' && *q!='-' && *q!='$' && *q!='('){ is_numeric=0; break; }
            }
            if(is_numeric && idx[0]){
                /* Check for negative index: ${arr[-1]} → arr[arr_count-1] */
                if(idx[0]=='-' && isdigit((unsigned char)idx[1])){
                    /* Negative index: __arr_<name>[__sh_arr_count(__arr_<name>)-N] */
                    snprintf(r,sizeof(r),"__arr_%s[__sh_arr_count(__arr_%s)%s]",safe_cname(name),safe_cname(name),idx);
                } else {
                    snprintf(r,sizeof(r),"__arr_%s[%s]",safe_cname(name),idx);
                }
            } else {
                /* String key — use associative lookup */
                /* If idx is $var, translate to runtime expression */
                if(idx[0]=='$' && idx[1]){
                    char *e=translate_expr(idx);
                    snprintf(r,sizeof(r),"__sh_assoc_get(__arr_%s,%s)",safe_cname(name),e);
                    free(e);
                } else {
                    snprintf(r,sizeof(r),"__sh_assoc_get(__arr_%s,\"%s\")",safe_cname(name),idx);
                }
            }
        }
        return xstrdup(r);
    }
    /* ${var:-default}  ${var:=default}  ${var:+alt}  ${var:?err} */
    if(*p==':' && (*(p+1)=='-'||*(p+1)=='='||*(p+1)=='+'||*(p+1)=='?')){
        char op=*(p+1);
        p+=2; char def[256]; int d=0;
        /* Read default value, handling nested ${...} */
        int brace_depth=0;
        while(*p && !(*p=='}' && brace_depth==0)){
            if(*p=='{') brace_depth++;
            else if(*p=='}') brace_depth--;
            if(d<255) def[d++]=*p;
            p++;
        }
        def[d]=0;
        char r[2048];
        const char *cn;
        char cnbuf[300];
        /* For := operator, auto-register unknown vars as V_STR */
        if(op=='=' && !isdigit((unsigned char)name[0]) && !is_known_var(name)){
            add_var(name,V_STR);
        }
        VarKind vk = get_var_kind(name);
        if(isdigit((unsigned char)name[0])){
            /* positional parameter: $1 -> __sh_arg1 */
            snprintf(cnbuf,sizeof(cnbuf),"__sh_arg%s",name);
            cn=cnbuf;
            vk = V_STR; /* positional params are strings */
        } else if(is_known_var(name)){
            cn=safe_cname(name);
        } else {
            /* unknown variable — use getenv */
            snprintf(cnbuf,sizeof(cnbuf),"(__sh_getenv(\"%s\"))",name);
            cn=cnbuf;
            vk = V_STR;
        }
        if(vk == V_INT){
            /* int variables are always "set" in C — use string representation */
            switch(op){
                case '-': snprintf(r,sizeof(r),"__sh_fmt(\"%%d\",%s)",cn); break;
                case '=': snprintf(r,sizeof(r),"__sh_fmt(\"%%d\",%s)",cn); break;
                case '+': snprintf(r,sizeof(r),"\"%s\"",def); break;
                case '?': snprintf(r,sizeof(r),"__sh_fmt(\"%%d\",%s)",cn); break;
                default:  snprintf(r,sizeof(r),"__sh_fmt(\"%%d\",%s)",cn); break;
            }
        } else {
            /* Check if default value contains $ (needs recursive expansion) */
            char *def_expanded = NULL;
            if(strchr(def,'$')){
                def_expanded = translate_expr(def);
            }
            char def_buf[512];
            const char *def_val;
            if(def_expanded){
                def_val = def_expanded;
            } else {
                snprintf(def_buf,sizeof(def_buf),"\"%s\"",def);
                def_val = def_buf;
            }
            switch(op){
                case '-': snprintf(r,sizeof(r),"(%s[0]?%s:%s)",cn,cn,def_val); break;
                case '=':
                    if(is_known_var(name))
                        snprintf(r,sizeof(r),"(%s[0]?%s:(strncpy(%s,%s,sizeof(%s)-1),%s))",cn,cn,cn,def_val,cn,cn);
                    else
                        snprintf(r,sizeof(r),"(%s[0]?%s:%s)",cn,cn,def_val);
                    break;
                case '+': snprintf(r,sizeof(r),"(%s[0]?%s:\"\")",cn,def_val); break;
                case '?': {
                    /* Escape def for C string literal */
                    char esc_def[512]; int ei=0;
                    for(const char *dp=def;*dp&&ei<(int)sizeof(esc_def)-4;dp++){
                        if(*dp=='"'){ esc_def[ei++]='\\'; esc_def[ei++]='"'; }
                        else if(*dp=='\\'){ esc_def[ei++]='\\'; esc_def[ei++]='\\'; }
                        else if(*dp=='\n'){ esc_def[ei++]='\\'; esc_def[ei++]='n'; }
                        else esc_def[ei++]=*dp;
                    }
                    esc_def[ei]=0;
                    snprintf(r,sizeof(r),"(%s[0]?%s:(fprintf(stderr,\"%s\\n\"),exit(1),\"\"))",cn,cn,esc_def);
                    break;
                }
                default:  snprintf(r,sizeof(r),"%s",cn); break;
            }
            if(def_expanded) free(def_expanded);
        }
        return xstrdup(r);
    }
    /* ${var:offset} or ${var:offset:length} — substring */
    if(*p==':'){
        p++; char off[32]; int k=0;
        while(*p && *p!=':' && *p!='}') off[k++]=*p++;
        off[k]=0;
        char len[32]=""; 
        if(*p==':'){ p++; int l=0; while(*p&&*p!='}') len[l++]=*p++; len[l]=0; }
        if(*p=='}') p++;
        char r[512];
        if(len[0])
            snprintf(r,sizeof(r),"__sh_substr(%s,%s,%s)",safe_cname(name),off,len);
        else
            snprintf(r,sizeof(r),"__sh_substr(%s,%s,-1)",safe_cname(name),off);
        return xstrdup(r);
    }
    /* ${var#pattern}  ${var##pattern} — prefix removal */
    if(*p=='#'){
        int greedy=(*(p+1)=='#'); if(greedy) p++;
        p++; char pat[128]; int k=0;
        while(*p && *p!='}') pat[k++]=*p++;
        pat[k]=0; if(*p) p++;
        char r[512];
        snprintf(r,sizeof(r),"__sh_strip_prefix(%s,\"%s\",%d)",safe_cname(name),pat,greedy);
        return xstrdup(r);
    }
    /* ${var%pattern}  ${var%%pattern} — suffix removal */
    if(*p=='%'){
        int greedy=(*(p+1)=='%'); if(greedy) p++;
        p++; char pat[128]; int k=0;
        while(*p && *p!='}') pat[k++]=*p++;
        pat[k]=0; if(*p) p++;
        char r[512];
        snprintf(r,sizeof(r),"__sh_strip_suffix(%s,\"%s\",%d)",safe_cname(name),pat,greedy);
        return xstrdup(r);
    }
    /* ${var/old/new}  ${var//old/new}  ${var/#old/new}  ${var/%old/new} */
    if(*p=='/'){
        int global=(*(p+1)=='/'); if(global) p++;
        int anchor_start=0, anchor_end=0;
        p++;
        if(*p=='#'){ anchor_start=1; p++; }
        if(*p=='%'){ anchor_end=1; p++; }
        char old[128]; int k=0;
        while(*p && *p!='/' && *p!='}') old[k++]=*p++;
        old[k]=0;
        char newp[128]="";
        if(*p=='/'){ p++; int l=0; while(*p&&*p!='}') newp[l++]=*p++; newp[l]=0; }
        if(*p=='}') p++;
        char r[512];
        const char *cn;
        char cnbuf[300];
        if(isdigit((unsigned char)name[0])){
            snprintf(cnbuf,sizeof(cnbuf),"__sh_arg%s",name);
            cn=cnbuf;
        } else if(is_known_var(name)){
            cn=safe_cname(name);
        } else {
            snprintf(cnbuf,sizeof(cnbuf),"(__sh_getenv(\"%s\"))",name);
            cn=cnbuf;
        }
        snprintf(r,sizeof(r),"__sh_replace(%s,\"%s\",\"%s\",%d,%d,%d)",
                 cn,old,newp,global,anchor_start,anchor_end);
        return xstrdup(r);
    }
    /* ${var^^}  ${var,,} — case conversion */
    if(*p=='^'){
        int all=(*(p+1)=='^'); if(all) p++;
        p++; if(*p=='}') p++;
        char r[256];
        snprintf(r,sizeof(r),"__sh_upper(%s)",safe_cname(name));
        return xstrdup(r);
    }
    if(*p==','){
        int all=(*(p+1)==','); if(all) p++;
        p++; if(*p=='}') p++;
        char r[256];
        snprintf(r,sizeof(r),"__sh_lower(%s)",safe_cname(name));
        return xstrdup(r);
    }
    /* plain ${var} */
    if(*p=='}') p++;
    return xstrdup(safe_cname(name));
}


/* Expand $var references in a command string for $(...) substitution.
 * Returns a C expression string that builds the expanded command.
 * If no variables found, returns NULL (caller uses literal). */
static char *expand_cmd_subst(const char *cmd){
    static char result[8192];
    char fmt[2048]; int fi=0;
    char args[2048]; int ai=0;
    int has_var=0;
    const char *p=cmd;
    while(*p && fi<(int)sizeof(fmt)-16){
        if(*p=='$'){
            p++;
            if(*p=='{'){
                p++;
                char nm[128]; int j=0;
                while(*p && *p!='}' && j<(int)sizeof(nm)-1) nm[j++]=*p++;
                nm[j]=0; if(*p) p++;
                VarKind vk=get_var_kind(nm);
                fmt[fi++]='%'; fmt[fi++]=(vk==V_INT)?'d':'s';
                if(ai>0) args[ai++]=',';
                ai+=snprintf(args+ai,sizeof(args)-ai,"%s",safe_cname(nm));
                has_var=1;
            } else if(*p=='?'){
                fmt[fi++]='%'; fmt[fi++]='d';
                if(ai>0) args[ai++]=',';
                ai+=snprintf(args+ai,sizeof(args)-ai,"__exit_status");
                p++; has_var=1;
            } else if(*p=='#'){
                fmt[fi++]='%'; fmt[fi++]='d';
                if(ai>0) args[ai++]=',';
                ai+=snprintf(args+ai,sizeof(args)-ai,"__sh_argc");
                p++; has_var=1;
            } else if(isdigit((unsigned char)*p)){
                fmt[fi++]='%'; fmt[fi++]='s';
                if(ai>0) args[ai++]=',';
                ai+=snprintf(args+ai,sizeof(args)-ai,"__sh_arg%c",*p);
                p++; has_var=1;
            } else if(isalpha((unsigned char)*p)||*p=='_'){
                char nm[128]; int j=0;
                while(isalnum((unsigned char)*p)||*p=='_'){
                    if(j<(int)sizeof(nm)-1) nm[j++]=*p;
                    p++;
                }
                nm[j]=0;
                /* Intercept bash special variables */
                if(!strcmp(nm,"RANDOM")||!strcmp(nm,"SECONDS")||!strcmp(nm,"LINENO")||
                   !strcmp(nm,"BASHPID")||!strcmp(nm,"SHLVL")||!strcmp(nm,"HOSTTYPE")||
                   !strcmp(nm,"OSTYPE")||!strcmp(nm,"MACHTYPE")){
                    fmt[fi++]='%'; fmt[fi++]='s';
                    if(ai>0) args[ai++]=',';
                    ai+=snprintf(args+ai,sizeof(args)-ai,"__sh_special_var(\"%s\")",nm);
                    has_var=1;
                } else {
                    VarKind vk=get_var_kind(nm);
                    fmt[fi++]='%'; fmt[fi++]=(vk==V_INT)?'d':'s';
                    if(ai>0) args[ai++]=',';
                    ai+=snprintf(args+ai,sizeof(args)-ai,"%s",safe_cname(nm));
                    has_var=1;
                }
            } else {
                fmt[fi++]='$';
            }
        } else {
            if(*p=='"'||*p=='\\') fmt[fi++]='\\';
            if(*p=='\n'){ fmt[fi++]='\\'; fmt[fi++]='n'; p++; continue; }
            if(*p=='\t'){ fmt[fi++]='\\'; fmt[fi++]='t'; p++; continue; }
            fmt[fi++]=*p++;
        }
    }
    fmt[fi]=0; args[ai]=0;
    if(!has_var) return NULL;
    snprintf(result,sizeof(result),"__sh_fmt(\"%s\",%s)",fmt,args);
    return result;
}

/* Translate a shell arithmetic expression (as used in for ((init;cond;update)))
 * into a C expression string. Returns heap string.
 * For string variables, uses atoi() for reads and snprintf() for writes. */
char *translate_arith(const char *expr);
char *translate_arith(const char *expr){
    if(!expr || !*expr) return xstrdup("");
    char buf[2048]; int q=0;
    const char *s=expr;
    while(*s==' '||*s=='\t') s++;
    while(*s && q<(int)sizeof(buf)-8){
        if(isalpha((unsigned char)*s)||*s=='_'){
            char nm[128]; int j=0;
            while(isalnum((unsigned char)*s)||*s=='_'){
                if(j<(int)sizeof(nm)-1) nm[j++]=*s;
                s++;
            }
            nm[j]=0;
            const char *cn=safe_cname(nm);
            VarKind vk=get_var_kind(nm);
            /* check for = assignment (not ==, <=, >=, !=) */
            const char *np=s; while(*np==' '||*np=='\t') np++;
            /* check for += compound assignment (bash only supports +=) */
            if(*np=='+' && *(np+1)=='=' && *(np+2)!='='){
                /* bash: x+=val means string append */
                q+=snprintf(buf+q,sizeof(buf)-q,"strncat(%s,",cn);
                s=np+2;
                while(*s==' '||*s=='\t') s++;
                /* read RHS */
                if(*s=='"'){
                    s++;
                    char rhs[256]; int ri=0;
                    while(*s && *s!='"' && ri<(int)sizeof(rhs)-1) rhs[ri++]=*s++;
                    if(*s=='"') s++;
                    rhs[ri]=0;
                    q+=snprintf(buf+q,sizeof(buf)-q,"\"%s\",sizeof(%s)-strlen(%s)-1)",rhs,cn,cn);
                } else {
                    /* unquoted — could be number or variable */
                    char rhs[256]; int ri=0;
                    while(*s && *s!=';' && *s!='"' && ri<(int)sizeof(rhs)-1){
                        rhs[ri++]=*s++;
                    }
                    rhs[ri]=0;
                    /* check if it's a number */
                    int is_num=1;
                    for(int i=0;i<ri;i++){
                        if(!isdigit((unsigned char)rhs[i]) && rhs[i]!='-'){ is_num=0; break; }
                    }
                    if(is_num){
                        q+=snprintf(buf+q,sizeof(buf)-q,"\"%s\",sizeof(%s)-strlen(%s)-1)",rhs,cn,cn);
                    } else {
                        /* variable — use its string value */
                        const char *cn2=safe_cname(rhs);
                        q+=snprintf(buf+q,sizeof(buf)-q,"%s,sizeof(%s)-strlen(%s)-1)",cn2,cn,cn);
                    }
                }
                buf[q++]=')';
                continue;
            }
            if(*np=='='&&*(np+1)!='='){
                /* assignment: var = expr */
                if(vk==V_STR){
                    /* snprintf(var, sizeof(var), "%d", (expr)) */
                    q+=snprintf(buf+q,sizeof(buf)-q,"snprintf(%s,sizeof(%s),\"%%d\",",cn,cn);
                } else {
                    if(!is_known_var(nm)) add_var(nm,V_INT);
                    cn=safe_cname(nm);
                    while(*cn) buf[q++]=*cn++;
                    buf[q++]='=';
                }
                /* skip the = */
                s=np+1;
                /* read the RHS expression */
                while(*s==' '||*s=='\t') s++;
                while(*s && *s!=';' && q<(int)sizeof(buf)-4){
                    if(isalpha((unsigned char)*s)||*s=='_'){
                        char nm2[128]; int j2=0;
                        while(isalnum((unsigned char)*s)||*s=='_'){
                            if(j2<(int)sizeof(nm2)-1) nm2[j2++]=*s;
                            s++;
                        }
                        nm2[j2]=0;
                        const char *cn2=safe_cname(nm2);
                        VarKind vk2=get_var_kind(nm2);
                        if(vk2==V_STR){
                            const char *pre="atoi("; while(*pre) buf[q++]=*pre++;
                            while(*cn2) buf[q++]=*cn2++;
                            buf[q++]=')';
                        } else {
                            while(*cn2) buf[q++]=*cn2++;
                        }
                    } else {
                        buf[q++]=*s++;
                    }
                }
                if(vk==V_STR) buf[q++]=')';
                continue;
            }
            /* check for ++ or -- */
            if(*np=='+'&&*(np+1)=='+'){
                if(vk==V_STR){
                    q+=snprintf(buf+q,sizeof(buf)-q,"snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)+1)",cn,cn,cn);
                } else {
                    while(*cn) buf[q++]=*cn++;
                    buf[q++]='+'; buf[q++]='+';
                }
                s=np+2;
                continue;
            }
            if(*np=='-'&&*(np+1)=='-'){
                if(vk==V_STR){
                    q+=snprintf(buf+q,sizeof(buf)-q,"snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)-1)",cn,cn,cn);
                } else {
                    while(*cn) buf[q++]=*cn++;
                    buf[q++]='-'; buf[q++]='-';
                }
                s=np+2;
                continue;
            }
            /* read context */
            if(vk==V_STR){
                const char *pre="atoi("; while(*pre) buf[q++]=*pre++;
                while(*cn) buf[q++]=*cn++;
                buf[q++]=')';
            } else {
                while(*cn) buf[q++]=*cn++;
            }
        } else if(*s=='$'){
            s++;
            if(*s=='{'){
                s++;
                char nm[128]; int j=0;
                while(*s && *s!='}') nm[j++]=*s++;
                nm[j]=0; if(*s) s++;
                char *e=translate_brace_expansion(nm);
                int el=(int)strlen(e);
                if(q+el<(int)sizeof(buf)-2){ memcpy(buf+q,e,el); q+=el; }
                free(e);
            } else if(isdigit((unsigned char)*s)){
                /* $1..$9 → atoi(__sh_argN) */
                int n_arg=*s-'0'; s++;
                if(n_arg>=1&&n_arg<=9){
                    const char *prefix="atoi(__sh_arg";
                    while(*prefix) buf[q++]=*prefix++;
                    buf[q++]='0'+n_arg; buf[q++]=')';
                }
                continue;
            } else if(isalpha((unsigned char)*s)||*s=='_'){
                char nm[128]; int j=0;
                while(isalnum((unsigned char)*s)||*s=='_') nm[j++]=*s++;
                nm[j]=0;
                const char *cn=safe_cname(nm);
                VarKind vk=get_var_kind(nm);
                if(vk==V_STR){
                    const char *pre="atoi("; while(*pre) buf[q++]=*pre++;
                    while(*cn) buf[q++]=*cn++;
                    buf[q++]=')';
                } else {
                    while(*cn) buf[q++]=*cn++;
                }
            }
        } else {
            /* Detect division/modulo by zero — emit runtime check + fallback */
            if(*s=='/'||*s=='%'){
                const char *np=s+1;
                while(*np==' '||*np=='\t') np++;
                if(*np=='0' && !isdigit((unsigned char)*(np+1)) && *(np+1)!='x' && *(np+1)!='X'){
                    /* /0 or %0 — emit runtime error + safe fallback */
                    /* Build expression string for error message */
                    char div_expr[64]; int di2=0;
                    /* Look back to find the dividend */
                    int bs2=q; while(bs2>0 && (isdigit((unsigned char)buf[bs2-1])||buf[bs2-1]==' ')) bs2--;
                    int be2=bs2; while(be2<q) div_expr[di2++]=buf[be2++];
                    div_expr[di2++]=' '; div_expr[di2++]=*s; div_expr[di2++]=' '; div_expr[di2++]='0'; div_expr[di2]=0;
                    q=bs2; /* remove dividend from buf — it'll be in the call */
                    q+=snprintf(buf+q,sizeof(buf)-q,"(__sh_div_zero(\"%s\"),1)",div_expr);
                    s=np+1; continue;
                }
            }
            /* ** → power operator */
            if(*s=='*'&&*(s+1)=='*'){
                /* Find base: last token in buf */
                int bs=q;
                while(bs>0 && (isalnum((unsigned char)buf[bs-1])||buf[bs-1]=='_')) bs--;
                if(bs<q){
                    char base[256]; int bl=q-bs;
                    memcpy(base,buf+bs,bl); base[bl]=0;
                    q=bs;
                    const char *fn="(int)__sh_pow("; while(*fn) buf[q++]=*fn++;
                    for(int bi=0;base[bi];bi++) buf[q++]=base[bi];
                    buf[q++]=',';
                    s+=2;
                    while(*s==' ') s++;
                    while(*s && *s!=')' && *s!=' ' && *s!='+' && *s!='-' && *s!='*' && *s!='/' && *s!='%' && q<(int)sizeof(buf)-4){
                        buf[q++]=*s++;
                    }
                    buf[q++]=')';
                    continue;
                }
            }
            buf[q++]=*s++;
        }
    }
    buf[q]=0;
    /* trim trailing whitespace */
    while(q>0 && (buf[q-1]==' '||buf[q-1]=='\t')) buf[--q]=0;
    return xstrdup(buf);
}

char *translate_expr(const char *tok){
    if(!tok) return xstrdup("0");
    /* Handle double-quoted strings containing ${...} or $var */
    if(tok[0]=='"' && tok[strlen(tok)-1]=='"'){
        int len=(int)strlen(tok);
        if(len>=2){
            char inner[1024];
            int n=len-2; if(n>=(int)sizeof(inner)) n=(int)sizeof(inner)-1;
            memcpy(inner,tok+1,n); inner[n]=0;
            /* if inner contains $, use expand_string */
            if(strchr(inner,'$')){
                ExpandResult er; expand_string(inner,&er);
                static char result[8192];
                int ri=0;
                ri+=snprintf(result+ri,sizeof(result)-ri,"__sh_fmt(\"");
                /* escape the format string for C string literal */
                for(int i=0;er.fmt[i] && ri<(int)sizeof(result)-16;i++){
                    if(er.fmt[i]=='"') result[ri++]='\\';
                    if(er.fmt[i]=='\\') result[ri++]='\\';
                    result[ri++]=er.fmt[i];
                }
                result[ri++]='"';
                for(int i=0;i<er.nargs;i++){
                    result[ri++]=',';
                    int al=(int)strlen(er.args[i]);
                    if(ri+al<(int)sizeof(result)-4){
                        memcpy(result+ri,er.args[i],al); ri+=al;
                    }
                }
                result[ri++]=')'; result[ri]=0;
                expand_free(&er);
                return xstrdup(result);
            }
            /* no $ inside — return as literal string */
            char *r=malloc(n+3);
            sprintf(r,"\"%s\"",inner);
            return r;
        }
    }
    /* $(( expr )) — arithmetic */
    if(strncmp(tok,"$((",3)==0){
        char buf[4096]; int q=0;
        const char *s=tok+3;
        int paren_depth=0; /* track nested parens to not stop at inner ) */
        while(*s){
            if(*s=='(') paren_depth++;
            if(*s==')'){
                if(paren_depth==0 && *(s+1)==')') break; /* end of $(( )) */
                paren_depth--;
            }
            if(*s=='$'){
                s++;
                /* Check for $(cmd...) command substitution inside $(( )) */
                /* But NOT $((...)) which is arithmetic */
                if(*s=='(' && *(s+1)!='('){
                    s++; /* skip ( */
                    char cmd[1024]; int ci=0; int d=1;
                    while(*s && d && ci<(int)sizeof(cmd)-1){
                        if(*s=='(') d++;
                        else if(*s==')'){ d--; if(d==0) break; }
                        cmd[ci++]=*s++;
                    }
                    cmd[ci]=0; if(*s==')') s++;
                    /* Check if cmd is a user function */
                    char fname[256]; int fi2=0;
                    const char *sp=cmd;
                    while(*sp==' '||*sp=='\t') sp++;
                    while(*sp && *sp!=' ' && *sp!='\t' && fi2<(int)sizeof(fname)-1) fname[fi2++]=*sp++;
                    fname[fi2]=0;
                    if(is_user_func(fname)){
                        /* Generate __sh_capture_fn call */
                        const char *pre="atoi(__sh_capture_fn((void(*)(int,char**))";
                        while(*pre) buf[q++]=*pre++;
                        const char *cn=safe_cname(fname);
                        while(*cn) buf[q++]=*cn++;
                        buf[q++]=',';
                        /* args */
                        const char *ap=cmd+fi2;
                        while(*ap==' '||*ap=='\t') ap++;
                        int nargs=0;
                        buf[q++]='0'; nargs=0;
                        /* parse args */
                        while(*ap && nargs<9){
                            while(*ap==' '||*ap=='\t') ap++;
                            if(!*ap) break;
                            char arg[256]; int al=0;
                            if(*ap=='"'){ ap++; while(*ap&&*ap!='"'&&al<255) arg[al++]=*ap++; if(*ap=='"')ap++; }
                            else { while(*ap&&*ap!=' '&&*ap!='\t'&&al<255) arg[al++]=*ap++; }
                            arg[al]=0;
                            if(al>0){
                                buf[q++]=','; buf[q++]='"';
                                for(int ai=0;ai<al&&q<(int)sizeof(buf)-4;ai++) buf[q++]=arg[ai];
                                buf[q++]='"'; nargs++;
                            }
                        }
                        buf[q++]=','; buf[q++]='N'; buf[q++]='U'; buf[q++]='L'; buf[q++]='L';
                        buf[q++]=')'; buf[q++]=')';
                    } else {
                        /* External command */
                        const char *pre="atoi(__sh_cmd_output(\"";
                        while(*pre) buf[q++]=*pre++;
                        /* escape cmd for C string */
                        for(int ei=0;cmd[ei]&&q<(int)sizeof(buf)-4;ei++){
                            if(cmd[ei]=='"') buf[q++]='\\';
                            if(cmd[ei]=='\\') buf[q++]='\\';
                            buf[q++]=cmd[ei];
                        }
                        const char *suf="\"))";
                        while(*suf) buf[q++]=*suf++;
                    }
                    continue;
                }
                char nm[128]; int j=0;
                if(*s=='{'){
                    s++;
                    /* Use translate_brace_expansion for ${...} inside $(( )) */
                    char brace_body[256]; int bj=0;
                    while(*s && *s!='}' && bj<(int)sizeof(brace_body)-1) brace_body[bj++]=*s++;
                    brace_body[bj]=0; if(*s) s++;
                    char *e=translate_brace_expansion(brace_body);
                    int el=(int)strlen(e);
                    if(q+el<(int)sizeof(buf)-2){ memcpy(buf+q,e,el); q+=el; }
                    free(e);
                    continue;
                } else if(isdigit((unsigned char)*s)){
                    int n_arg=0;
                    while(isdigit((unsigned char)*s)) n_arg=n_arg*10+(*s++)-'0';
                    if(n_arg>=1&&n_arg<=9){
                        const char *prefix="atoi(__sh_arg";
                        while(*prefix) buf[q++]=*prefix++;
                        buf[q++]='0'+n_arg; buf[q++]=')';
                        continue;
                    }
                } else {
                    while(isalnum((unsigned char)*s)||*s=='_') nm[j++]=*s++;
                    nm[j]=0;
                }
                if(j>0){
                    VarKind vk=get_var_kind(nm);
                    const char *cn=safe_cname(nm);
                    /* Check for array access: var[$key] or var[N] */
                    if(vk==V_ARRAY && *s=='['){
                        s++; /* skip [ */
                        /* Read the index expression until ] */
                        char idxexpr[256]; int ii=0;
                        int d=1;
                        while(*s && d && ii<255){
                            if(*s=='[') d++;
                            else if(*s==']'){ d--; if(d==0) break; }
                            idxexpr[ii++]=*s++;
                        }
                        idxexpr[ii]=0; if(*s==']') s++;
                        /* Translate the index expression */
                        char *ie=translate_arith(idxexpr);
                        const char *pre="atoi(__arr_";
                        while(*pre) buf[q++]=*pre++;
                        while(*cn) buf[q++]=*cn++;
                        buf[q++]='['; buf[q++]='a'; buf[q++]='t'; buf[q++]='o'; buf[q++]='i'; buf[q++]='(';
                        const char *iep=ie;
                        while(*iep) buf[q++]=*iep++;
                        free(ie);
                        buf[q++]=')'; buf[q++]=']'; buf[q++]=')';
                        continue;
                    }
                    if(vk==V_STR){
                        const char *pre="atoi("; const char *suf=")";
                        while(*pre) buf[q++]=*pre++;
                        while(*cn) buf[q++]=*cn++;
                        while(*suf) buf[q++]=*suf++;
                    } else {
                        while(*cn) buf[q++]=*cn++;
                    }
                }
            } else if(*s=='(' && *(s+1)=='('){
                /* nested (( — copy verbatim, balance parens */
                int d=0;
                while(*s){
                    if(*s=='(') d++;
                    else if(*s==')'){ d--; if(d==0){ s++; break; } }
                    buf[q++]=*s++;
                }
            } else if(isalpha((unsigned char)*s)||*s=='_'){
                /* bare variable name (no $ prefix) — look up and translate */
                char nm[128]; int j=0;
                while(isalnum((unsigned char)*s)||*s=='_'){
                    if(j<(int)sizeof(nm)-1) nm[j++]=*s;
                    s++;
                }
                nm[j]=0;
                VarKind vk=get_var_kind(nm);
                const char *cn=safe_cname(nm);
                /* Check for array access: var[idx] or var[$key] */
                if(vk==V_ARRAY && *s=='['){
                    s++; /* skip [ */
                    char idxexpr[256]; int ii=0;
                    int bd=1;
                    while(*s && bd && ii<255){
                        if(*s=='[') bd++;
                        else if(*s==']'){ bd--; if(bd==0) break; }
                        idxexpr[ii++]=*s++;
                    }
                    idxexpr[ii]=0; if(*s==']') s++;
                    char *ie=translate_arith(idxexpr);
                    /* ie is already an int expression from translate_arith */
                    q+=snprintf(buf+q,sizeof(buf)-q,"atoi(__arr_%s[%s])",cn,ie);
                    free(ie);
                    continue;
                }
                /* check for = assignment (not ==) — register as int var */
                {
                    const char *np=s; while(*np==' '||*np=='\t') np++;
                    if(*np=='='&&*(np+1)!='='){
                        add_var(nm,V_INT);
                        vk=V_INT; cn=safe_cname(nm);
                    }
                }
                /* check for += compound assignment (bash string append) */
                if(*s=='+'&&*(s+1)=='='&&*(s+2)!='='){
                    /* x+=val → strncat(x, val, sizeof(x)-strlen(x)-1) */
                    s+=2;
                    while(*s==' '||*s=='\t') s++;
                    if(*s=='"'){
                        s++;
                        char rhs[256]; int ri=0;
                        while(*s && *s!='"' && ri<(int)sizeof(rhs)-1) rhs[ri++]=*s++;
                        if(*s=='"') s++;
                        rhs[ri]=0;
                        q+=snprintf(buf+q,sizeof(buf)-q,"strncat(%s,\"%s\",sizeof(%s)-strlen(%s)-1)",cn,rhs,cn,cn);
                    } else {
                        char rhs[256]; int ri=0;
                        while(*s && *s!=';' && *s!='"' && ri<(int)sizeof(rhs)-1) rhs[ri++]=*s++;
                        rhs[ri]=0;
                        int is_num=1;
                        for(int i=0;i<ri;i++){
                            if(!isdigit((unsigned char)rhs[i]) && rhs[i]!='-'){ is_num=0; break; }
                        }
                        if(is_num){
                            q+=snprintf(buf+q,sizeof(buf)-q,"strncat(%s,\"%s\",sizeof(%s)-strlen(%s)-1)",cn,rhs,cn,cn);
                        } else {
                            const char *cn2=safe_cname(rhs);
                            q+=snprintf(buf+q,sizeof(buf)-q,"strncat(%s,%s,sizeof(%s)-strlen(%s)-1)",cn,cn2,cn,cn);
                        }
                    }
                    continue;
                }
                /* check for ++ or -- after variable (post-increment/decrement) */
                if(vk==V_STR && (*s=='+'&&*(s+1)=='+')){
                    /* var++ → (atoi(var), snprintf(var,...,"%d",atoi(var)+1), atoi(var)-1) */
                    s+=2;
                    snprintf(buf+q,sizeof(buf)-q,
                        "(atoi(%s),(snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)+1),0))",cn,cn,cn,cn);
                    q=(int)strlen(buf);
                } else if(vk==V_STR && (*s=='-'&&*(s+1)=='-')){
                    s+=2;
                    snprintf(buf+q,sizeof(buf)-q,
                        "(atoi(%s),(snprintf(%s,sizeof(%s),\"%%d\",atoi(%s)-1),0))",cn,cn,cn,cn);
                    q=(int)strlen(buf);
                } else if(vk==V_STR){
                    const char *pre="atoi(";
                    while(*pre) buf[q++]=*pre++;
                    while(*cn) buf[q++]=*cn++;
                    buf[q++]=')';
                } else {
                    while(*cn) buf[q++]=*cn++;
                }
            } else {
                /* translate shell arithmetic ops to C */
                if(*s=='*'&&*(s+1)=='*'){
                    /* ** → power operator. Use __sh_pow.
                     * We emit a marker and close it at the next operator/space.
                     * For simple cases like x ** 2, this works. */
                    /* Find the base: last token in buf */
                    int bs=q;
                    while(bs>0 && (isalnum((unsigned char)buf[bs-1])||buf[bs-1]=='_')) bs--;
                    if(bs<q){
                        char base[256]; int bl=q-bs;
                        memcpy(base,buf+bs,bl); base[bl]=0;
                        q=bs;
                        const char *fn="(int)__sh_pow("; while(*fn) buf[q++]=*fn++;
                        for(int bi=0;base[bi];bi++) buf[q++]=base[bi];
                        buf[q++]=',';
                        s+=2;
                        /* now read the exponent */
                        while(*s==' ') s++;
                        while(*s && *s!=')' && *s!=' ' && *s!='+' && *s!='-' && *s!='*' && *s!='/' && *s!='%' && q<(int)sizeof(buf)-4){
                            buf[q++]=*s++;
                        }
                        buf[q++]=')';
                    } else {
                        buf[q++]='*'; s+=2;
                    }
                    continue;
                }
                /* ++ -- += -= *= /= %= <<= >>= &= |= ^= etc. pass through */
                /* Also handle two-char comparison ops: <= >= == != */
                if((*s=='<'||*s=='>'||*s=='='||*s=='!') && *(s+1)=='='){
                    buf[q++]=*s++; buf[q++]=*s++; continue;
                }
                if((*s=='<'||*s=='>') && (*(s+1)=='<'||*(s+1)=='>')){
                    /* << >> bit shift */
                    buf[q++]=*s++; buf[q++]=*s++; continue;
                }
                if((*s=='&'&&*(s+1)=='&')||(*s=='|'&&*(s+1)=='|')){
                    buf[q++]=*s++; buf[q++]=*s++; continue;
                }
                /* Detect division/modulo by zero — replace /0 with (1/(!0)) which is
                 * a no-op that avoids SIGFPE. Set exit_status. */
                if(*s=='/'||*s=='%'){
                    const char *np=s+1;
                    while(*np==' '||*np=='\t') np++;
                    if(*np=='0' && !isdigit((unsigned char)*(np+1)) && *(np+1)!='x' && *(np+1)!='X'){
                        q+=snprintf(buf+q,sizeof(buf)-q,"/1");
                        s=np+1; continue;
                    }
                }
                buf[q++]=*s++;
            }
        }
        buf[q]=0; return xstrdup(buf);
    }
    /* $( ... ) command substitution — emit a runtime helper call */
    if(strncmp(tok,"$(",2)==0){
        const char *s=tok+2;
        char cmd[2048]; int k=0;
        int d=1;
        while(*s && d){
            if(*s=='(') d++;
            else if(*s==')') { d--; if(d==0) break; }
            cmd[k++]=*s++;
        }
        cmd[k]=0;
        /* Check if the command is a user-defined function */
        char fname[256]; int fi=0;
        const char *sp=cmd;
        while(*sp==' '||*sp=='\t') sp++;
        while(*sp && *sp!=' ' && *sp!='\t' && fi<(int)sizeof(fname)-1) fname[fi++]=*sp++;
        fname[fi]=0;
        if(is_user_func(fname)){
            /* Build args array and count */
            char args[2048]; int ai=0; int nargs=0;
            ai += snprintf(args+ai,sizeof(args)-ai,"(char*[]){");
            while(*sp){
                while(*sp==' '||*sp=='\t') sp++;
                if(!*sp) break;
                char arg[512]; int ar=0;
                if(*sp=='"'){
                    sp++;
                    while(*sp && *sp!='"' && ar<(int)sizeof(arg)-1){
                        if(*sp=='\\' && *(sp+1)) sp++;
                        arg[ar++]=*sp++;
                    }
                    if(*sp=='"') sp++;
                } else if(*sp=='\''){
                    sp++;
                    while(*sp && *sp!='\'' && ar<(int)sizeof(arg)-1) arg[ar++]=*sp++;
                    if(*sp=='\'') sp++;
                } else if(*sp=='$' && *(sp+1)=='('){
                    /* $(...) or $((...)) — read as single unit */
                    arg[ar++]='$'; sp++;
                    arg[ar++]='('; sp++;
                    int d=1;
                    while(*sp && d && ar<(int)sizeof(arg)-1){
                        if(*sp=='(') d++;
                        else if(*sp==')') d--;
                        arg[ar++]=*sp++;
                    }
                } else {
                    while(*sp && *sp!=' ' && *sp!='\t' && ar<(int)sizeof(arg)-1) arg[ar++]=*sp++;
                }
                arg[ar]=0;
                /* translate the argument */
                if(arg[0]=='$'){
                    char *e=translate_expr(arg);
                    ai += snprintf(args+ai,sizeof(args)-ai,"%s,",e);
                    free(e);
                } else {
                    ai += snprintf(args+ai,sizeof(args)-ai,"\"%s\",",arg);
                }
                nargs++;
            }
            ai += snprintf(args+ai,sizeof(args)-ai,"NULL}");
            char r[4096];
            snprintf(r,sizeof(r),"__sh_capture_fn((void(*)(int,char**))%s,%d,%s)",
                     safe_cname(fname),nargs,args);
            return xstrdup(r);
        }
        char r[4096];
        char *expanded=expand_cmd_subst(cmd);
        if(expanded){
            snprintf(r,sizeof(r),"__sh_cmd_output(%s)",expanded);
        } else {
            char esc[2100]; int e=0;
            for(int i=0;cmd[i]&&e<(int)sizeof(esc)-4;i++){
                if(cmd[i]=='"'||cmd[i]=='\\') esc[e++]='\\';
                if(cmd[i]=='\n') { esc[e++]='\\'; esc[e++]='n'; continue; }
                if(cmd[i]=='\t') { esc[e++]='\\'; esc[e++]='t'; continue; }
                esc[e++]=cmd[i];
            }
            esc[e]=0;
            snprintf(r,sizeof(r),"__sh_cmd_output(\"%s\")",esc);
        }
        return xstrdup(r);
    }
    if(tok[0]=='$'){
        const char *p=tok+1; char name[256]; int j=0;
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
            return translate_brace_expansion(body);
        }
        if(*p=='?') return xstrdup("__exit_status");
        if(*p=='#') return xstrdup("__sh_argc");
        if(*p=='$') return xstrdup("(int)getpid()");
        if(*p=='!') return xstrdup("__sh_last_bg_pid");
        if(*p=='@'||*p=='*') return xstrdup("__sh_join_args(__sh_argc,__sh_args)");
        if(*p=='_') return xstrdup("__sh_last_arg");
        if(isdigit((unsigned char)*p)){
            char r[32]; snprintf(r,sizeof(r),"__sh_arg%c",*p);
            return xstrdup(r);
        }
        while(isalnum((unsigned char)*p)||*p=='_') name[j++]=*p++;
        name[j]=0;
        /* Intercept bash special variables */
        if(!strcmp(name,"RANDOM")||!strcmp(name,"SECONDS")||!strcmp(name,"LINENO")||
           !strcmp(name,"BASHPID")||!strcmp(name,"SHLVL")||!strcmp(name,"HOSTTYPE")||
           !strcmp(name,"OSTYPE")||!strcmp(name,"MACHTYPE")){
            char r[512]; snprintf(r,sizeof(r),"__sh_special_var(\"%s\")",name);
            return xstrdup(r);
        }
        return xstrdup(var_c_expr(name));
    }
    return xstrdup(tok);
}

