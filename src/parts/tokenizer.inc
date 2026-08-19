/* ================================================================== */
/* L4  Tokenizer                                                      */
/* ================================================================== */

static char _tok_pool[524288];
static int  _tok_pool_pos=0;

static char *pool_dup(const char *s,int len){
    if(_tok_pool_pos+len+1>=(int)sizeof(_tok_pool)) return xstrdup(s);
    char *d=_tok_pool+_tok_pool_pos;
    memcpy(d,s,len); d[len]=0;
    _tok_pool_pos+=len+1;
    return d;
}

/* Multi-char operators recognized by the tokenizer */
int tokenize(const char *line, char **toks, int maxtoks){
    _tok_pool_pos=0;
    int n=0; const char *p=line;
    while(*p && n<maxtoks-1){
        while(*p==' '||*p=='\t') p++;
        if(!*p) break;
        /* comments */
        if(*p=='#'){ break; }
        /* double-quoted string (keep quotes, but convert backticks to $(...)) */
        if(*p=='"'){
            const char *s=p; p++;
            /* check if there are backticks inside */
            const char *has_bt=NULL;
            { const char *q=p; while(*q && *q!='"'){ if(*q=='\\'&&*(q+1)) q+=2; else if(*q=='`'){has_bt=q;break;} else q++; } }
            if(has_bt){
                /* expand backticks to $(...) inside the quoted string */
                char buf[4096]; int bi=0;
                buf[bi++]='"';
                while(*p && *p!='"' && bi<(int)sizeof(buf)-3){
                    if(*p=='\\' && *(p+1)=='`'){ buf[bi++]='`'; p+=2; }
                    else if(*p=='\\' && *(p+1)=='\\'){ buf[bi++]='\\'; p+=2; }
                    else if(*p=='\\' && *(p+1)=='$'){ buf[bi++]='$'; p+=2; }
                    else if(*p=='`'){
                        p++;
                        buf[bi++]='$'; buf[bi++]='(';
                        while(*p && *p!='`' && bi<(int)sizeof(buf)-3){
                            if(*p=='\\' && *(p+1)=='`'){ buf[bi++]='`'; p+=2; }
                            else if(*p=='\\' && *(p+1)=='\\'){ buf[bi++]='\\'; p+=2; }
                            else if(*p=='\\' && *(p+1)=='$'){ buf[bi++]='$'; p+=2; }
                            else buf[bi++]=*p++;
                        }
                        if(*p=='`') p++;
                        buf[bi++]=')';
                    } else if(*p=='\\' && *(p+1)=='"'){ buf[bi++]='\\'; buf[bi++]='"'; p+=2; }
                    else { buf[bi++]=*p++; }
                }
                if(*p=='"') p++;
                buf[bi++]='"'; buf[bi]=0;
                toks[n++]=pool_dup(buf,bi); continue;
            }
            while(*p && *p!='"'){
                if(*p=='\\'&&*(p+1)) { p+=2; continue; }
                /* Skip $(...) and $((...)) inside double-quoted strings */
                if(*p=='$'&&*(p+1)=='('){
                    p+=2; int d=1;
                    while(*p&&d){
                        if(*p=='"'){ p++; while(*p&&*p!='"'){ if(*p=='\\'&&*(p+1)){p+=2;continue;} p++; } if(*p)p++; continue; }
                        if(*p=='\''){ p++; while(*p&&*p!='\'')p++; if(*p)p++; continue; }
                        if(*p=='(')d++; else if(*p==')')d--;
                        p++;
                    }
                    continue;
                }
                /* Skip ${...} inside double-quoted strings — skip inner quotes too */
                if(*p=='$'&&*(p+1)=='{'){
                    p+=2; int d=1;
                    while(*p&&d){
                        if(*p=='"'){ p++; while(*p&&*p!='"'){ if(*p=='\\'&&*(p+1)){p+=2;continue;} p++; } if(*p)p++; continue; }
                        if(*p=='\''){ p++; while(*p&&*p!='\'')p++; if(*p)p++; continue; }
                        if(*p=='{')d++; else if(*p=='}')d--;
                        p++;
                    }
                    continue;
                }
                p++;
            }
            if(*p=='"') p++;
            /* consume trailing bare chars (e.g. } after "str"}) */
            while(*p && !isspace((unsigned char)*p) && *p!=';' && *p!='|' && *p!='&'
                   && *p!='<' && *p!='>' && *p!='(' && *p!=')' && *p!='[' && *p!=']'){
                /* Skip ${...} as a unit so ${arr[-1]} isn't split at [ */
                if(*p=='$' && *(p+1)=='{'){
                    p+=2; int bd=1;
                    while(*p&&bd){
                        if(*p=='{')bd++; else if(*p=='}')bd--;
                        if(bd>0) p++;
                    }
                    if(*p=='}') p++;
                    continue;
                }
                p++;
            }
            toks[n++]=pool_dup(s,(int)(p-s)); continue;
        }
        /* Note: standalone single-quoted strings are handled by the bare word
         * handler below, which also handles mixed quoting like 'It'\''s' */
        /* backtick command substitution — convert to $(...) form */
        if(*p=='`'){
            p++;
            char buf[2048]; int bi=0;
            buf[bi++]='$'; buf[bi++]='(';
            while(*p && *p!='`' && bi<(int)sizeof(buf)-3){
                if(*p=='\\' && *(p+1)=='`'){ buf[bi++]='`'; p+=2; }
                else if(*p=='\\' && *(p+1)=='\\'){ buf[bi++]='\\'; p+=2; }
                else if(*p=='\\' && *(p+1)=='$'){ buf[bi++]='$'; p+=2; }
                else { buf[bi++]=*p++; }
            }
            if(*p=='`') p++;
            buf[bi++]=')'; buf[bi]=0;
            toks[n++]=pool_dup(buf,bi); continue;
        }
        /* multi-char operators */
        if(*p==';'&&*(p+1)==';'){ toks[n++]=(char*)";;"; p+=2; continue; }
        if(*p=='&'&&*(p+1)=='&'){ toks[n++]=(char*)"&&"; p+=2; continue; }
        if(*p=='|'&&*(p+1)=='|'){ toks[n++]=(char*)"||"; p+=2; continue; }
        if(*p=='<'&&*(p+1)=='<'){
            if(*(p+2)=='<'){ toks[n++]=(char*)"<<<"; p+=3; continue; }
            toks[n++]=(char*)"<<"; p+=2; continue;
        }
        /* process substitution: <(...) or >(...) */
        if((*p=='<'||*p=='>')&&*(p+1)=='('){
            char ps_buf[2048]; int pi=0;
            ps_buf[pi++]=*p++; ps_buf[pi++]='('; p++; /* skip the ( */
            int d=1;
            while(*p && d && pi<(int)sizeof(ps_buf)-2){
                if(*p=='(') d++;
                else if(*p==')'){ d--; if(d==0) break; }
                ps_buf[pi++]=*p++;
            }
            if(*p==')') p++;
            ps_buf[pi++]=')'; ps_buf[pi]=0;
            toks[n++]=pool_dup(ps_buf,pi); continue;
        }
        if(*p=='>'&&*(p+1)=='>'){ toks[n++]=(char*)">>"; p+=2; continue; }
        if(*p=='<'&&*(p+1)=='='){ toks[n++]=(char*)"<="; p+=2; continue; }
        if(*p=='>'&&*(p+1)=='='){ toks[n++]=(char*)">="; p+=2; continue; }
        if(*p=='='&&*(p+1)=='='){ toks[n++]=(char*)"=="; p+=2; continue; }
        if(*p=='!'&&*(p+1)=='='){ toks[n++]=(char*)"!="; p+=2; continue; }
        if(*p=='<'&&*(p+1)=='>'){ toks[n++]=(char*)"<>"; p+=2; continue; }
        if(*p=='&'&&*(p+1)=='>'){ toks[n++]=(char*)"&>"; p+=2; continue; }
        if(*p==';'){             toks[n++]=(char*)";";  p++;  continue; }
        if(*p=='|'){             toks[n++]=(char*)"|";  p++;  continue; }
        if(*p=='&'){             toks[n++]=(char*)"&";  p++;  continue; }
        if(*p=='['&&*(p+1)=='['){ toks[n++]=(char*)"[["; p+=2; continue; }
        if(*p==']'&&*(p+1)==']'){ toks[n++]=(char*)"]]"; p+=2; continue; }
        if(*p=='['){             toks[n++]=(char*)"[";  p++;  continue; }
        if(*p==']'){             toks[n++]=(char*)"]";  p++;  continue; }
        /* { and } as standalone tokens when surrounded by whitespace or start/end */
        if(*p=='{' && (p==line || isspace((unsigned char)*(p-1))) && (isspace((unsigned char)*(p+1)) || *(p+1)==';' || *(p+1)==0)){
            toks[n++]=(char*)"{"; p++; continue;
        }
        if(*p=='}' && (isspace((unsigned char)*(p-1)) || *(p-1)==';') && (isspace((unsigned char)*(p+1)) || *(p+1)==';' || *(p+1)==0)){
            toks[n++]=(char*)"}"; p++; continue;
        }
        if(*p=='('&&*(p+1)=='('){ toks[n++]=(char*)"(("; p+=2; continue; }
        if(*p==')'&&*(p+1)==')'){ toks[n++]=(char*)"))"; p+=2; continue; }
        if(*p=='('){             toks[n++]=(char*)"(";  p++;  continue; }
        if(*p==')'){             toks[n++]=(char*)")";  p++;  continue; }
        if(*p=='<'&&*(p+1)=='&'){ toks[n++]=(char*)"<&"; p+=2; continue; }
        if(*p=='>'&&*(p+1)=='&'){ toks[n++]=(char*)">&"; p+=2; continue; }
        if(*p=='<'){             toks[n++]=(char*)"<";  p++;  continue; }
        if(*p=='>'){             toks[n++]=(char*)">";  p++;  continue; }
        /* bare word — consumes adjacent quoted segments too (e.g. 'It'\''s' = one token) */
        { const char *s=p;
          while(*p&&*p!=' '&&*p!='\t'){
              if(*p=='\\'&&*(p+1)){ p+=2; continue; }  /* escape next char */
              if(*p=='#'&&p>s) break;
              if(*p=='\''){
                  /* consume single-quoted segment as part of this word */
                  p++;
                  while(*p && *p!='\'') p++;
                  if(*p=='\'') p++;
                  continue;
              }
              if(*p=='"'){
                  /* consume double-quoted segment as part of this word */
                  p++;
                  while(*p && *p!='"'){
                      if(*p=='\\'&&*(p+1)){ p+=2; continue; }
                      if(*p=='$'&&*(p+1)=='('){ p+=2; int d=1; while(*p&&d){ if(*p=='(')d++; else if(*p==')')d--; p++; } continue; }
                      p++;
                  }
                  if(*p=='"') p++;
                  continue;
              }
              /* check for array index pattern: name[key]= or name[key] */
              if(*p=='[' && p>s && (isalnum((unsigned char)*(p-1))||*(p-1)=='_')){
                  /* look for matching ] */
                  const char *rb=p+1; int bd=1;
                  while(*rb && bd){
                      if(*rb=='[') bd++;
                      else if(*rb==']') bd--;
                      if(bd) rb++;
                  }
                  if(*rb==']' && (*(rb+1)=='='||(*(rb+1)=='+'&&*(rb+2)=='=')||*(rb+1)==0||*(rb+1)==' '||*(rb+1)==';')){
                      /* array index pattern — skip to after ] */
                      p=rb+1;
                      /* if followed by =, include the =value part */
                      if(*p=='='){
                          p++;
                          /* include quoted value */
                          if(*p=='"'){ p++; while(*p&&*p!='"'){ if(*p=='\\'&&*(p+1))p++; p++; } if(*p=='"')p++; }
                          else if(*p=='\''){ p++; while(*p&&*p!='\'') p++; if(*p=='\'')p++; }
                          else { while(*p&&*p!=' '&&*p!='\t'&&*p!=';'&&*p!='&'&&*p!='|') p++; }
                      } else if(*p=='+'&&*(p+1)=='='){
                          p+=2;
                          if(*p=='"'){ p++; while(*p&&*p!='"'){ if(*p=='\\'&&*(p+1))p++; p++; } if(*p=='"')p++; }
                          else { while(*p&&*p!=' '&&*p!='\t'&&*p!=';'&&*p!='&'&&*p!='|') p++; }
                      }
                      continue;
                  }
                  break; /* not an array pattern, break on [ */
              }
              if(*p==';'||*p=='['||*p==']'||*p=='('||*p==')') break;
              if(*p=='|'&&*(p+1)!='|') break;
              if(*p=='&'&&*(p+1)!='&') break;
              if(*p=='<'||*p=='>') break;
              /* backtick inside word — break word, let backtick handler process */
              if(*p=='`') break;
              /* $((…)) intact */
              if(*p=='$'&&*(p+1)=='('&&*(p+2)=='('){
                  p++; int d=0;
                  while(*p){if(*p=='(')d++;else if(*p==')'&&--d==0){p++;break;}p++;}
                  continue;
              }
              /* $(...) command substitution kept intact */
              if(*p=='$'&&*(p+1)=='('){
                  p+=2; int d=1;
                  while(*p&&d){ if(*p=='(')d++; else if(*p==')')d--; p++; }
                  continue;
              }
              if(*p=='$'&&*(p+1)=='{'){
                  p+=2; while(*p&&*p!='}')p++; if(*p=='}')p++; continue;
              }
              p++;
          }
          if(p>s){ toks[n++]=pool_dup(s,(int)(p-s)); }
        }
    }
    toks[n]=NULL; return n;
}

/* Expand brace patterns in a token list.
 * Handles: {a,b,c}, {1..10}, {a..z}, prefix{a,b}suffix */
int expand_braces(char **toks, int ntoks, int maxtoks){
    char *out[4096]; int no=0;
    for(int i=0;i<ntoks && no<maxtoks-1;i++){
        char *t=toks[i];
        /* find { in the token (but not ${, and not inside double quotes) */
        char *bp=NULL;
        int in_dq=0;
        for(char *p=t;*p;p++){
            if(*p=='"' && (p==t||*(p-1)!='\\')) in_dq=!in_dq;
            if(!in_dq && *p=='{' && (p==t||*(p-1)!='$')){
                /* BUG-18 fix: skip { inside $(...) */
                /* Check if we're inside a $(...) — if so, don't expand */
                int in_cmd_subst=0;
                for(char *q=t;q<p;q++){
                    if(*q=='$'&&*(q+1)=='(') in_cmd_subst++;
                    if(*q==')'&&in_cmd_subst>0) in_cmd_subst--;
                }
                if(in_cmd_subst>0) continue; /* skip brace expansion inside $(...) */
                /* check for matching } */
                int d=1; char *q=p+1;
                while(*q && d){ if(*q=='{')d++; else if(*q=='}')d--; if(d)q++; }
                if(*q=='}'){ bp=p; break; }
            }
        }
        if(!bp){ out[no++]=t; continue; }
        /* find matching } */
        char *ep=bp+1; int d=1;
        while(*ep && d){ if(*ep=='{')d++; else if(*ep=='}')d--; if(d)ep++; }
        /* extract prefix, body, suffix */
        char prefix[512]; int plen=(int)(bp-t);
        memcpy(prefix,t,plen); prefix[plen]=0;
        char suffix[512]; strncpy(suffix,ep+1,sizeof(suffix)-1); suffix[sizeof(suffix)-1]=0;
        char body[512]; int blen=(int)(ep-bp-1);
        memcpy(body,bp+1,blen); body[blen]=0;
        /* check for range: {1..10} or {a..z} */
        char *dotdot=strstr(body,"..");
        if(dotdot){
            char start[64]; int sl=(int)(dotdot-body);
            memcpy(start,body,sl); start[sl]=0;
            char *end=dotdot+2;
            if(start[0] && end[0]){
                /* numeric range */
                if(isdigit((unsigned char)start[0]) && isdigit((unsigned char)end[0])){
                    int s=atoi(start), e=atoi(end);
                    if(s<=e){ for(int v=s;v<=e&&no<1024;v++){
                        char buf[1200]; snprintf(buf,sizeof(buf),"%s%d%s",prefix,v,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                    else { for(int v=s;v>=e&&no<1024;v--){
                        char buf[1200]; snprintf(buf,sizeof(buf),"%s%d%s",prefix,v,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                }
                /* alpha range */
                if(strlen(start)==1 && strlen(end)==1){
                    char s=start[0], e=end[0];
                    if(s<=e){ for(char c=s;c<=e&&no<1024;c++){
                        char buf[1200]; snprintf(buf,sizeof(buf),"%s%c%s",prefix,c,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                }
            }
        }
        /* comma-separated list: {a,b,c} */
        {
            char *items[256]; int ni=0;
            char *p=body; char *start=p;
            int dd=0;
            while(*p){
                if(*p=='{') dd++;
                else if(*p=='}') dd--;
                else if(*p==',' && dd==0){
                    if(ni<63){ items[ni++]=start; *p=0; }
                    start=p+1;
                }
                p++;
            }
            if(ni<63) items[ni++]=start;
            if(ni>1 || (ni==1 && strlen(items[0])>0)){
                for(int j=0;j<ni&&no<1024;j++){
                    char buf[1024]; snprintf(buf,sizeof(buf),"%s%s%s",prefix,items[j],suffix);
                    out[no++]=pool_dup(buf,(int)strlen(buf));
                }
                continue;
            }
        }
        out[no++]=t;
    }
    /* copy back */
    int result = no<maxtoks?no:maxtoks;
    for(int i=0;i<result;i++) toks[i]=out[i];
    toks[result]=NULL;
    return result;
}

