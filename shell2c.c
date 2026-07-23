/*
 * shell2c.c — Deep Shell-to-C Transpiler (Modular Entry Point)
 *
 * Author: 爱摸鱼的狐狸 🦊
 *
 * Modular architecture with code obfuscation for generated C output.
 * Headers in src/ define the module interfaces; this file contains
 * the full implementation compiled as a single translation unit for
 * simplicity (amalgamation build).
 *
 * Build:  gcc -O2 -Wall -o shell2c shell2c.c src/s2c_obfuscate.c
 * Use:    ./shell2c input.sh output.c [--makefile] [--run] [--obfuscate]
 *
 * Modules:
 *   s2c_common.h    — shared types, utilities, safe-naming
 *   s2c_symtab.h    — variable/function/heredoc tables
 *   s2c_ast.h       — AST node types and Redir struct
 *   s2c_emit.h      — code emitter interface
 *   s2c_parse.h     — parser and tokenizer interface
 *   s2c_obfuscate.h — anti-analysis code generation
 */

#include "src/s2c_common.h"
#include "src/s2c_symtab.h"
#include "src/s2c_ast.h"
#include "src/s2c_emit.h"
#include "src/s2c_parse.h"
#include "src/s2c_obfuscate.h"
#include "src/s2c_mangle.h"

/* ================================================================== */
/* L0  Utility — definitions now in s2c_common.h                       */
/* ================================================================== */

/* xstrdup, ltrim, rtrim, trim, starts_with, C_KEYWORDS, safe_cname
 * are defined in src/s2c_common.h as static inline */
/* ================================================================== */

/* All L0/L1 definitions (xstrdup, ltrim, rtrim, trim, starts_with,
 * C_KEYWORDS, safe_cname) are now in src/s2c_common.h.
 * The C_KEYWORDS list below is the full version with C library names. */

/* Extended C keyword list (includes C library function names) */
#undef C_KEYWORDS
static const char *C_KEYWORDS_EXT[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","inline","int","long",
    "register","restrict","return","short","signed","sizeof","static","struct",
    "switch","typedef","union","unsigned","void","volatile","while",
    "and","or","not","true","false","NULL","bool","complex","imaginary",
    "main","printf","fprintf","sprintf","snprintf","scanf","fscanf","sscanf",
    "malloc","calloc","realloc","free","exit","abort","atoi","atol","atof",
    "strdup","strndup","strlen","strcmp","strncmp","strcpy","strncpy","strcat","strncat",
    "memcpy","memmove","memset","memcmp","strstr","strchr","strrchr","strpbrk",
    "open","close","read","write","lseek","dup","dup2","pipe","fork","exec",
    "wait","waitpid","getpid","getppid","getuid","getgid","getenv","setenv",
    "chdir","getcwd","opendir","readdir","closedir","stat","fstat","lstat",
    NULL
};

/* Override safe_cname to use extended keyword list */
static char _cname_buf[256];
const char *safe_cname(const char *name){
    if(!name||!*name) return "__sh_empty";
    for(int i=0;C_KEYWORDS_EXT[i];i++){
        if(strcmp(name,C_KEYWORDS_EXT[i])==0){
            snprintf(_cname_buf,sizeof(_cname_buf),"_sh_%s",name);
            return _cname_buf;
        }
    }
    if(!isalpha((unsigned char)name[0]) && name[0]!='_'){
        snprintf(_cname_buf,sizeof(_cname_buf),"_sh_%s",name);
        return _cname_buf;
    }
    int dirty=0;
    for(const char *p=name;*p;p++){
        if(!isalnum((unsigned char)*p) && *p!='_'){ dirty=1; break; }
    }
    if(dirty){
        char tmp[256]; int j=0;
        for(const char *p=name;*p && j<(int)sizeof(tmp)-1;p++){
            tmp[j++] = (isalnum((unsigned char)*p)||*p=='_') ? *p : '_';
        }
        tmp[j]=0;
        snprintf(_cname_buf,sizeof(_cname_buf),"%s",tmp);
        return _cname_buf;
    }
    return name;
}

/* ================================================================== */
/* L2  Symbol table — types in s2c_symtab.h, definitions here          */
/* ================================================================== */

/* VarKind, VarInfo, var_table, var_count declared in s2c_symtab.h */
VarInfo var_table[MAX_VARS];
int var_count = 0;

void add_var(const char *name, VarKind k){
    if(!name||!*name) return;
    for(int i=0;i<var_count;i++){
        if(strcmp(var_table[i].name,name)==0){
            if(k==V_ARRAY) var_table[i].kind=V_ARRAY;
            else if(k==V_INT) var_table[i].kind=V_INT;
            return;
        }
    }
    if(var_count>=MAX_VARS) return;
    strncpy(var_table[var_count].name,name,127);
    var_table[var_count].name[127]=0;
    var_table[var_count].kind=k;
    var_table[var_count].is_local=0;
    var_count++;
}
VarKind get_var_kind(const char *name){
    if(!name) return V_STR;
    for(int i=0;i<var_count;i++)
        if(strcmp(var_table[i].name,name)==0) return var_table[i].kind;
    return V_STR;
}
int is_known_var(const char *name){
    if(!name) return 0;
    for(int i=0;i<var_count;i++)
        if(strcmp(var_table[i].name,name)==0) return 1;
    return 0;
}
const char *var_c_expr(const char *name){
    if(is_known_var(name)) return safe_cname(name);
    static char buf[256];
    snprintf(buf,sizeof(buf),"(__sh_getenv(\"%s\"))",name);
    return buf;
}

/* Function table */
char *func_table[MAX_FUNCS];
int func_count = 0;
void register_func(const char *name){
    if(!name||!*name) return;
    for(int i=0;i<func_count;i++) if(strcmp(func_table[i],name)==0) return;
    if(func_count<MAX_FUNCS) func_table[func_count++]=xstrdup(name);
}
int is_user_func(const char *name){
    if(!name||!*name) return 0;
    for(int i=0;i<func_count;i++) if(strcmp(func_table[i],name)==0) return 1;
    return 0;
}

/* Heredoc table */
HeredocEntry heredoc_table[MAX_HEREDOCS];
int heredoc_count = 0;
int heredoc_next = 0;
int heredoc_store(const char *text, int expand){
    if(heredoc_count>=MAX_HEREDOCS) return -1;
    heredoc_table[heredoc_count].text = xstrdup(text);
    heredoc_table[heredoc_count].expand = expand;
    return heredoc_count++;
}
const char *heredoc_consume(int *expand){
    if(heredoc_next>=heredoc_count){ if(expand)*expand=1; return ""; }
    if(expand) *expand = heredoc_table[heredoc_next].expand;
    return heredoc_table[heredoc_next++].text;
}

/* ================================================================== */
/* L3  AST — types in s2c_ast.h, constructors here                     */
/* ================================================================== */

/* NodeType, Redir, Node are declared in s2c_ast.h.
 * The local Node struct has extra fields (set_opts) not in the header,
 * so we use a local extension. */
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
    char *case_pats[64]; struct NodeExt *case_bodies[64]; int case_count;
    struct NodeExt *case_default;
    /* trap */
    char *trap_action; int trap_sig;
    /* set */
    char *set_opts;
} NodeExt;

/* Use NodeExt as Node throughout this file */
#define Node NodeExt

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

/* ================================================================== */
/* L4  Tokenizer                                                      */
/* ================================================================== */

static char _tok_pool[131072];
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
                    while(*p&&d){ if(*p=='(')d++; else if(*p==')')d--; p++; }
                    continue;
                }
                p++;
            }
            if(*p=='"') p++;
            toks[n++]=pool_dup(s,(int)(p-s)); continue;
        }
        if(*p=='\''){
            const char *s=p; p++;
            while(*p && *p!='\'') p++;
            if(*p=='\'') p++;
            toks[n++]=pool_dup(s,(int)(p-s)); continue;
        }
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
        if(*p=='<'&&*(p+1)=='>'){ toks[n++]=(char*)"<>"; p+=2; continue; }
        if(*p=='&'&&*(p+1)=='>'){ toks[n++]=(char*)"&>"; p+=2; continue; }
        if(*p==';'){             toks[n++]=(char*)";";  p++;  continue; }
        if(*p=='|'){             toks[n++]=(char*)"|";  p++;  continue; }
        if(*p=='&'){             toks[n++]=(char*)"&";  p++;  continue; }
        if(*p=='['&&*(p+1)=='['){ toks[n++]=(char*)"[["; p+=2; continue; }
        if(*p==']'&&*(p+1)==']'){ toks[n++]=(char*)"]]"; p+=2; continue; }
        if(*p=='['){             toks[n++]=(char*)"[";  p++;  continue; }
        if(*p==']'){             toks[n++]=(char*)"]";  p++;  continue; }
        /* { and } are NOT separate tokens — they're part of words for brace expansion.
         * Standalone { and } (surrounded by spaces) will be bare word tokens. */
        if(*p=='('&&*(p+1)=='('){ toks[n++]=(char*)"(("; p+=2; continue; }
        if(*p==')'&&*(p+1)==')'){ toks[n++]=(char*)"))"; p+=2; continue; }
        if(*p=='('){             toks[n++]=(char*)"(";  p++;  continue; }
        if(*p==')'){             toks[n++]=(char*)")";  p++;  continue; }
        if(*p=='<'&&*(p+1)=='&'){ toks[n++]=(char*)"<&"; p+=2; continue; }
        if(*p=='>'&&*(p+1)=='&'){ toks[n++]=(char*)">&"; p+=2; continue; }
        if(*p=='<'){             toks[n++]=(char*)"<";  p++;  continue; }
        if(*p=='>'){             toks[n++]=(char*)">";  p++;  continue; }
        /* bare word */
        { const char *s=p;
          while(*p&&*p!=' '&&*p!='\t'&&*p!='"'&&*p!='\''){
              if(*p=='\\'&&*(p+1)){ p+=2; continue; }  /* escape next char */
              if(*p=='#'&&p>s) break;
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
          if(p>s) toks[n++]=pool_dup(s,(int)(p-s));
        }
    }
    toks[n]=NULL; return n;
}

/* Expand brace patterns in a token list.
 * Handles: {a,b,c}, {1..10}, {a..z}, prefix{a,b}suffix */
int expand_braces(char **toks, int ntoks, int maxtoks){
    char *out[1024]; int no=0;
    for(int i=0;i<ntoks && no<1024;i++){
        char *t=toks[i];
        /* find { in the token (but not ${, and not inside double quotes) */
        char *bp=NULL;
        int in_dq=0;
        for(char *p=t;*p;p++){
            if(*p=='"' && (p==t||*(p-1)!='\\')) in_dq=!in_dq;
            if(!in_dq && *p=='{' && (p==t||*(p-1)!='$')){
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
        char suffix[512]; strcpy(suffix,ep+1);
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
                        char buf[600]; snprintf(buf,sizeof(buf),"%s%d%s",prefix,v,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                    else { for(int v=s;v>=e&&no<1024;v--){
                        char buf[600]; snprintf(buf,sizeof(buf),"%s%d%s",prefix,v,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                }
                /* alpha range */
                if(strlen(start)==1 && strlen(end)==1){
                    char s=start[0], e=end[0];
                    if(s<=e){ for(char c=s;c<=e&&no<1024;c++){
                        char buf[600]; snprintf(buf,sizeof(buf),"%s%c%s",prefix,c,suffix);
                        out[no++]=pool_dup(buf,(int)strlen(buf));
                    } continue; }
                }
            }
        }
        /* comma-separated list: {a,b,c} */
        {
            char *items[64]; int ni=0;
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
            snprintf(r,sizeof(r),"__arr_%s[%s]",safe_cname(name),idx);
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
                case '?': snprintf(r,sizeof(r),"(%s[0]?%s:(fprintf(stderr,\%s\\n\),exit(1),\\))",cn,cn,def); break;
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
                VarKind vk=get_var_kind(nm);
                fmt[fi++]='%'; fmt[fi++]=(vk==V_INT)?'d':'s';
                if(ai>0) args[ai++]=',';
                ai+=snprintf(args+ai,sizeof(args)-ai,"%s",safe_cname(nm));
                has_var=1;
            } else {
                fmt[fi++]='$';
            }
        } else {
            if(*p=='"'||*p=='\\') fmt[fi++]='\\';
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
        while(*s && !(*s==')' && *(s+1)==')')){
            if(*s=='$'){
                s++;
                char nm[128]; int j=0;
                if(*s=='{'){
                    s++;
                    while(*s && *s!='}') nm[j++]=*s++;
                    nm[j]=0; if(*s) s++;
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
                /* check for = assignment (not ==) — register as int var */
                {
                    const char *np=s; while(*np==' '||*np=='\t') np++;
                    if(*np=='='&&*(np+1)!='='){
                        add_var(nm,V_INT);
                        vk=V_INT; cn=safe_cname(nm);
                    }
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
                /* ++ -- += -= etc. pass through */
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
        if(*p=='@'||*p=='*') return xstrdup("\"\"");
        if(*p=='_') return xstrdup("__sh_last_arg");
        if(isdigit((unsigned char)*p)){
            char r[32]; snprintf(r,sizeof(r),"__sh_arg%c",*p);
            return xstrdup(r);
        }
        while(isalnum((unsigned char)*p)||*p=='_') name[j++]=*p++;
        name[j]=0;
        return xstrdup(var_c_expr(name));
    }
    return xstrdup(tok);
}

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
                        if(*p=='{'){
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
                    else { expr[k++]=*p++; }
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
                    char *expanded=expand_cmd_subst(cmd);
                    if(expanded){
                        snprintf(r,sizeof(r),"__sh_cmd_output(%s)",expanded);
                    } else {
                        char esc[1050]; int e=0;
                        for(int i=0;cmd[i]&&e<(int)sizeof(esc)-4;i++){
                            if(cmd[i]=='"'||cmd[i]=='\\') esc[e++]='\\';
                            esc[e++]=cmd[i];
                        }
                        esc[e]=0;
                        snprintf(r,sizeof(r),"__sh_cmd_output(\"%s\")",esc);
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
            if(er->nargs<EXPAND_MAX_ARGS){ er->args[er->nargs]=xstrdup("\"\""); er->arg_is_int[er->nargs++]=0; }
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
    if(tok[0]!='$' && !strchr(tok,'$') && get_var_kind(tok)==V_UNKNOWN
       && strncmp(tok,"$((",3)!=0){
        const char *p=tok; if(*p=='-'||*p=='+') p++;
        int isnum=*p!='\0';
        for(const char *q=p;*q;q++){ if(!isdigit((unsigned char)*q)){ isnum=0; break; } }
        if(!isnum){
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
    const char *s=cond;
    while(isspace((unsigned char)*s)) s++;
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
        /* simple approach: split into words, build expression */
        char *words[64]; int nw=0;
        char *save; char *cp=strdup(s);
        char *tk=strtok_r(cp," \t",&save);
        while(tk && nw<64){ words[nw++]=tk; tk=strtok_r(NULL," \t",&save); }
        words[nw]=NULL;
        int wi=0; int bi=0;
        bi+=snprintf(buf+bi,sizeof(buf)-bi,"(");
        while(wi<nw){
            if(!strcmp(words[wi],"&&")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"&&"); wi++; }
            else if(!strcmp(words[wi],"||")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"||"); wi++; }
            else if(!strcmp(words[wi],"!")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"!"); wi++; }
            else if(!strcmp(words[wi],"(")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,"("); wi++; }
            else if(!strcmp(words[wi],")")){ bi+=snprintf(buf+bi,sizeof(buf)-bi,")"); wi++; }
            else {
                /* look ahead for binary op */
                if(wi+2<nw && (
                    !strcmp(words[wi+1],"=")||!strcmp(words[wi+1],"==")||
                    !strcmp(words[wi+1],"!=")||!strcmp(words[wi+1],"<")||
                    !strcmp(words[wi+1],">")||!strcmp(words[wi+1],"=~")||
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
                            fai+=snprintf(fargs+fai,sizeof(fargs)-fai,"\"%s\",",words[j]);
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
        free(cp);
        return buf;
    }
    /* [ ... ] */
    if(*s=='['){
        s++;
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
                ai+=snprintf(args+ai,sizeof(args)-ai,"\"%s\",",arg);
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
    char inner[4096];
    /* Single-quoted strings are literal — no variable expansion */
    if(word[0]=='\'' && strlen(word)>=2){
        strncpy(inner,word+1,sizeof(inner)-1); inner[sizeof(inner)-1]=0;
        int l=(int)strlen(inner);
        if(l>0 && inner[l-1]=='\'') inner[--l]=0;
        char *r=malloc(strlen(inner)+3);
        sprintf(r,"\"%s\"",inner);
        return r;
    }
    if((word[0]=='"')&&strlen(word)>=2){
        strncpy(inner,word+1,sizeof(inner)-1); inner[sizeof(inner)-1]=0;
        int l=(int)strlen(inner);
        if(l>0&&inner[l-1]=='"') inner[--l]=0;
    } else {
        strncpy(inner,word,sizeof(inner)-1); inner[sizeof(inner)-1]=0;
    }
    if(!strchr(inner,'$')&&!strchr(inner,'\\')){
        /* Escape embedded newlines/tabs for C string literal */
        char esc[8192]; int ei=0;
        for(int i=0;inner[i]&&ei<(int)sizeof(esc)-4;i++){
            if(inner[i]=='\n'){ esc[ei++]='\\'; esc[ei++]='n'; }
            else if(inner[i]=='\t'){ esc[ei++]='\\'; esc[ei++]='t'; }
            else if(inner[i]=='\r'){ esc[ei++]='\\'; esc[ei++]='r'; }
            else if(inner[i]=='"'){ esc[ei++]='\\'; esc[ei++]='"'; }
            else esc[ei++]=inner[i];
        }
        esc[ei]=0;
        char *r=malloc(strlen(esc)+3);
        sprintf(r,"\"%s\"",esc);
        return r;
    }
    ExpandResult er; expand_string(word,&er);
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
        if(ac>=2){
            /* printf format string: don't escape % like echo does */
            const char *fmt = argv[1];
            int flen = (int)strlen(fmt);
            int is_literal = (flen>=2 && fmt[0]=='"' && fmt[flen-1]=='"');
            if(is_literal && !strchr(fmt,'$')){
                /* Literal format string — use directly */
                fprintf(out,"    __sh_printf(%s", fmt);
            } else {
                /* Has variables — use emit_word but fix %% back to % */
                char *w=emit_word(out,argv[1]);
                fprintf(out,"    { const char *__pf=%s; __sh_printf(__pf",w);
                free(w);
            }
            for(int i=2;i<ac;i++){ char *a=emit_word(out,argv[i]); fprintf(out,",%s",a); free(a); }
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
        if(ac>=3){
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
        for(int i=1;i<ac;i++){
            if(!strcmp(argv[i],"-r")||!strcmp(argv[i],"-p")) continue;
            if(argv[i][0]=='-') continue;
            if(nv<7) vars[nv++]=argv[i];
        }
        if(nv==0) vars[nv++]="REPLY";
        fprintf(out,"    { char __rb[4096]; if(fgets(__rb,sizeof(__rb),stdin)){ int __rl=(int)strlen(__rb); if(__rl>0&&__rb[__rl-1]=='\\n')__rb[--__rl]=0;");
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
            /* find next free index in array */
            fprintf(out,"    { int __ai=0; while(__arr_%s[__ai]) __ai++;\n",safe_cname(n->lhs));
            /* if key is numeric, use as index; otherwise just append */
            int is_num_key=1;
            for(const char *q=akey;*q;q++){ if(!isdigit((unsigned char)*q)){ is_num_key=0; break; } }
            if(is_num_key && akey[0]){
                fprintf(out,"    int __idx=atoi(\"%s\");\n",akey);
                fprintf(out,"    if(__idx>=64) __idx=63;\n");
                fprintf(out,"    __arr_%s[__idx]=",safe_cname(n->lhs));
            } else {
                fprintf(out,"    __arr_%s[__ai++]=",safe_cname(n->lhs));
            }
            /* emit value */
            if(aval[0]=='"'){
                char inner[1024]; int il=0;
                const char *vp=aval+1;
                while(*vp && *vp!='"' && il<(int)sizeof(inner)-1) inner[il++]=*vp++;
                inner[il]=0;
                if(strchr(inner,'$')){
                    ExpandResult er; expand_string(inner,&er);
                    fprintf(out,"__sh_fmt(\"%s\"",er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    fprintf(out,");\n");
                    expand_free(&er);
                } else {
                    fprintf(out,"strdup(\"%s\");\n",inner);
                }
            } else if(strchr(aval,'$')){
                ExpandResult er; expand_string(aval,&er);
                fprintf(out,"__sh_fmt(\"%s\"",er.fmt);
                for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                fprintf(out,");\n");
                expand_free(&er);
            } else {
                fprintf(out,"strdup(\"%s\");\n",aval);
            }
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
                if(*p=='"'){ p++; while(*p&&*p!='"'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='"')p++; }
                else if(*p=='\''){ p++; while(*p&&*p!='\''&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='\'')p++; }
                else { while(*p&&*p!=' '&&*p!='\t'&&*p!=')'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; }
                elem[ei]=0;
                fprintf(out,"    __arr_%s[__al++]=\"%s\";\n",safe_cname(n->lhs),elem);
            }
            fprintf(out,"    __arr_%s[__al]=NULL; }\n",safe_cname(n->lhs));
            break;
        }
        if(n->lineno==-2){
            /* string append: var+=value */
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
                fprintf(out,"    strncat(%s,\"%s\",sizeof(%s)-strlen(%s)-1);\n",cn,n->rhs,cn,cn);
            }
            break;
        }
        if(n->rhs && n->rhs[0]=='('){
            /* array */
            add_var(n->lhs,V_ARRAY);
            const char *p=n->rhs+1;
            char items[64][256]; int ni=0;
            while(*p && *p!=')'){
                while(*p==' '||*p=='\t') p++;
                if(*p==')') break;
                char *q=items[ni]; int k=0;
                while(*p && *p!=' ' && *p!='\t' && *p!=')'){
                    if(*p=='"'){ p++; while(*p&&*p!='"'){ if(*p=='\\'&&*(p+1))p++; q[k++]=*p++; } if(*p)p++; }
                    else if(*p=='\''){ p++; while(*p&&*p!='\'') q[k++]=*p++; if(*p)p++; }
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
                q[k]=0; if(k>0){ ni++; }
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
                    fprintf(out,"    { char __tw_%d[4096]; snprintf(__tw_%d,sizeof(__tw_%d),\"%s\"",id,id,id,er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    fprintf(out,");\n");
                    expand_free(&er);
                    fprintf(out,"    char *__sp=__tw_%d;\n",id);
                    fprintf(out,"    while(*__sp){ while(*__sp==' '||*__sp=='\\t')__sp++; if(!*__sp)break;\n");
                    fprintf(out,"    char *__se=__sp; while(*__se&&*__se!=' '&&*__se!='\\t')__se++;\n");
                    fprintf(out,"    char __sc=*__se; *__se=0; __arr_%s[__an%d++]=strdup(__sp); *__se=__sc; __sp=__se; if(*__sp)__sp++; }\n",safe_cname(n->lhs),n->lineno);
                    fprintf(out,"    }\n");
                } else if(strchr(items[i],'$')){
                    /* Quoted or mixed item with $ — expand as single element */
                    ExpandResult er; expand_string(items[i],&er);
                    int id=tmp_id++;
                    fprintf(out,"    char __tw_%d[4096]; snprintf(__tw_%d,sizeof(__tw_%d),\"%s\"",id,id,id,er.fmt);
                    for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                    fprintf(out,");\n");
                    expand_free(&er);
                    fprintf(out,"    __arr_%s[__an%d++]=strdup(__tw_%d);\n",safe_cname(n->lhs),n->lineno,id);
                } else {
                    fprintf(out,"    __arr_%s[__an%d++]=strdup(\"%s\");\n",safe_cname(n->lhs),n->lineno,items[i]);
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
                    strncpy(tmp,val+1,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
                    int l=(int)strlen(tmp);
                    if(l>0&&(tmp[l-1]=='"'||tmp[l-1]=='\'')) tmp[--l]=0;
                    fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",
                            safe_cname(n->lhs),tmp,safe_cname(n->lhs));
                } else {
                    fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",
                            safe_cname(n->lhs),val,safe_cname(n->lhs));
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
                    while(*p && *p!=')'){
                        while(*p==' '||*p=='\t') p++;
                        if(*p==')') break;
                        char elem[256]; int ei=0;
                        if(*p=='"'){ p++; while(*p&&*p!='"'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='"')p++; }
                        else if(*p=='\''){ p++; while(*p&&*p!='\''&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; if(*p=='\'')p++; }
                        else { while(*p&&*p!=' '&&*p!='\t'&&*p!=')'&&ei<(int)sizeof(elem)-1) elem[ei++]=*p++; }
                        elem[ei]=0;
                        fprintf(out,"    __arr_%s[%d]=\"%s\";\n",cn,idx,elem);
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
                        while(*vp && *vp!=val[0] && il<(int)sizeof(inner)-1) inner[il++]=*vp++;
                        inner[il]=0;
                        if(strchr(inner,'$')){
                            /* expand variables */
                            ExpandResult er; expand_string(inner,&er);
                            fprintf(out,"    snprintf(%s,sizeof(%s),\"%s\"",cn,cn,er.fmt);
                            for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                            fprintf(out,");\n");
                            expand_free(&er);
                        } else {
                            fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",cn,inner,cn);
                        }
                    } else if(strchr(val,'$')){
                        ExpandResult er; expand_string(val,&er);
                        fprintf(out,"    snprintf(%s,sizeof(%s),\"%s\"",cn,cn,er.fmt);
                        for(int ai=0;ai<er.nargs;ai++) fprintf(out,",%s",er.args[ai]);
                        fprintf(out,");\n");
                        expand_free(&er);
                    } else {
                        fprintf(out,"    strncpy(%s,\"%s\",sizeof(%s)-1);\n",cn,val,cn);
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
        char *argv[64]; int ac=0;
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
                const char *vname = safe_cname(n->for_list[0]+1);
                fprintf(out,"    {\n");
                fprintf(out,"    char __sbuf[4096]; strncpy(__sbuf,%s,sizeof(__sbuf)-1); __sbuf[sizeof(__sbuf)-1]=0;\n",vname);
                fprintf(out,"    char *__sv=__sbuf;\n");
                fprintf(out,"    while(*__sv){\n");
                fprintf(out,"        while(*__sv==' '||*__sv=='\\t'||*__sv=='\\n')__sv++;\n");
                fprintf(out,"        if(!*__sv)break;\n");
                fprintf(out,"        char *__se=__sv;\n");
                fprintf(out,"        while(*__se&&*__se!=' '&&*__se!='\\t'&&*__se!='\\n')__se++;\n");
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
                fprintf(out,"    char __sbuf[4096]; strncpy(__sbuf,%s,sizeof(__sbuf)-1); __sbuf[sizeof(__sbuf)-1]=0;\n",w);
                free(w);
                fprintf(out,"    char *__sv=__sbuf;\n");
                fprintf(out,"    while(*__sv){\n");
                fprintf(out,"        while(*__sv==' '||*__sv=='\\t'||*__sv=='\\n')__sv++;\n");
                fprintf(out,"        if(!*__sv)break;\n");
                fprintf(out,"        char *__se=__sv;\n");
                fprintf(out,"        while(*__se&&*__se!=' '&&*__se!='\\t'&&*__se!='\\n')__se++;\n");
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
            fprintf(out,"    const char *__fl%d[] = {",n->lineno);
            for(int i=0;i<n->for_len;i++){
                char *w=emit_word(out,n->for_list[i]);
                fprintf(out,"%s%s",(i>0?", ":""),w);
                free(w);
            }
            fprintf(out,", NULL};\n");
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
        /* Check if condition is a read command (while read -r line) */
        if(n->while_cond && !strncmp(n->while_cond,"read",4) &&
           (n->while_cond[4]==' '||n->while_cond[4]==0)){
            /* Parse read variables from condition */
            char vars[8][128]; int nv=0;
            const char *p=n->while_cond+4;
            while(*p && nv<8){
                while(*p==' '||*p=='\t') p++;
                if(!*p) break;
                if(*p=='-'){ p++; while(*p&&*p!=' ')p++; continue; }
                int vl=0;
                while(*p && *p!=' ' && *p!='\t' && vl<127) vars[nv][vl++]=*p++;
                vars[nv][vl]=0;
                if(vl>0){ add_var(vars[nv],V_STR); nv++; }
            }
            fprintf(out,"    { char __rb[4096];\n");
            if(n->while_negate){
                fprintf(out,"    while(!fgets(__rb,sizeof(__rb),stdin)){\n");
            } else {
                fprintf(out,"    while(fgets(__rb,sizeof(__rb),stdin)){\n");
            }
            fprintf(out,"    int __rl=(int)strlen(__rb); if(__rl>0&&__rb[__rl-1]=='\\n')__rb[--__rl]=0;\n");
            if(nv==1){
                fprintf(out,"    strncpy(%s,__rb,sizeof(%s)-1);\n",safe_cname(vars[0]),safe_cname(vars[0]));
            } else if(nv>1){
                fprintf(out,"    char *__sp=__rb; int __vi=0; char *__tok=strtok(__sp,\" \\t\");\n");
                fprintf(out,"    while(__tok&&__vi<%d){ switch(__vi){\n",nv);
                for(int i=0;i<nv;i++)
                    fprintf(out,"    case %d: strncpy(%s,__tok,sizeof(%s)-1); break;\n",i,safe_cname(vars[i]),safe_cname(vars[i]));
                fprintf(out,"    } __vi++; __tok=strtok(NULL,\" \\t\"); }\n");
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
        break;
    }

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
                char patcopy[256]; strncpy(patcopy,pat,255); patcopy[255]=0;
                char *tok=strtok(patcopy,"|");
                int first=1;
                while(tok){
                    if(!first) fprintf(out,"||");
                    /* check if pattern has wildcards for fnmatch */
                    int has_glob = (strchr(tok,'*')||strchr(tok,'?')||strchr(tok,'['));
                    if(has_glob){
                        fprintf(out,"(fnmatch(\"%s\",%s,0)==0)",tok,cv);
                    } else {
                        fprintf(out,"(strcmp(%s, \"%s\") == 0)",cv,tok);
                    }
                    first=0; tok=strtok(NULL,"|");
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
        fprintf(out,"    __b_trap(\"%s\",%d);\n",n->trap_action?n->trap_action:"",n->trap_sig);
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
            fprintf(out,"    char __sh_arg%d[1024]=\"\"; if(__sh_argc>=%d)strncpy(__sh_arg%d,__sh_args[%d-1],1023);\n",
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

/* ================================================================== */
/* L9  Parser                                                         */
/* ================================================================== */

/* BlkFrame, BlkKind, blk_stack, blk_top, parse_root, parse_insert
 * are declared in s2c_parse.h. Definitions here: */
BlkFrame blk_stack[STACK_MAX];
int blk_top=0;
Node *parse_root=NULL;
Node **parse_insert=NULL;
/* pending_pipe_cmd and pipe_restore_needed declared in L8 emitter section */

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
    if(blk_top>=STACK_MAX){fprintf(stderr,"stack overflow\n");return;}
    blk_stack[blk_top++]=fr;
}

static void parser_pop(void){
    if(blk_top<=0) return;
    Node **pi=blk_stack[--blk_top].parent_insert;
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
        for(int i=1;i<n;i++){
            if(toks[i][0]=='-') continue;  /* skip flags like -r, -p */
            if(!strcmp(toks[i],"<<<")||!strcmp(toks[i],"<<")) break;  /* stop at here-string/heredoc */
            add_var(toks[i],V_STR);
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
    if((p=strstr(tmp,"; do")))*p=0;
    else if((p=strstr(tmp,";do")))*p=0;
    else if((p=strstr(tmp," do")))*p=0;
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
            if(blk_top>0&&blk_stack[blk_top-1].kind==BLK_CASE) parser_pop();
            return;
        }
        /* BLK_CASE body */
        if(blk_top>0&&blk_stack[blk_top-1].kind==BLK_CASE){
            Node *cnd=blk_stack[blk_top-1].node;
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
                        Node *bc=make_cmd(toks+cs,ce-cs,lineno);
                        parser_append(bc);
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
            char arith[2100];
            snprintf(arith,sizeof(arith),"$((%s))",expr);
            char *e=translate_expr(arith);
            Node *nd=new_node(NODE_CMD,lineno);
            nd->argv=malloc(3*sizeof(char*));
            nd->argv[0]=xstrdup("__arith");
            nd->argv[1]=xstrdup(e);
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
            if(blk_top>0){
                BlkFrame *fr=&blk_stack[blk_top-1]; Node *pif=fr->node;
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
            if(blk_top>0){
                BlkFrame *fr=&blk_stack[blk_top-1]; Node *pif=fr->node;
                fr->kind=BLK_IF_ELSE;
                parse_insert=&pif->else_blk;
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
                    strncpy(init_str,buf,sizeof(init_str)-1); init_str[sizeof(init_str)-1]=0;
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
                /* skip VAR=VALUE prefixes (e.g. IFS= read -r line) */
                if(is_assignment(toks[i]) && i<ntoks-1 && strcmp(toks[i+1],"do")!=0 && strcmp(toks[i+1],";")!=0){
                    continue;
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
        if(!strcmp(kw,"done")){ parser_pop(); return; }

        if(!strcmp(kw,"case")){
            Node *nd=new_node(NODE_CASE,lineno);
            nd->case_var=(ntoks>1)?xstrdup(translate_expr(toks[1])):xstrdup("\"\"");
            Node **_pi=parse_insert;
            parser_append(nd);
            BlkFrame fr={BLK_CASE,nd,NULL,_pi};
            parser_push(fr); parse_insert=NULL; return;
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
              } else if(ntoks>=3&&!strcmp(toks[1],"(")&&!strcmp(toks[2],")")){
                  strncpy(fname,kw,127); is_func=1;
              }
          }
          if(is_func){
              Node *nd=new_node(NODE_FUNC,lineno); nd->fname=xstrdup(fname);
              register_func(fname);
              Node **_pi=parse_insert;
              parser_append(nd);
              BlkFrame fr={BLK_FUNC,nd,&nd->func_body,_pi};
              parser_push(fr); parse_insert=&nd->func_body; return;
          }
        }
        if(!strcmp(kw,"}")){
            if(blk_top>0&&(blk_stack[blk_top-1].kind==BLK_FUNC||
                            blk_stack[blk_top-1].kind==BLK_CASE||
                            blk_stack[blk_top-1].kind==BLK_GROUP))
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
            int is_int=0, is_array=0, is_readonly=0; (void)is_readonly;
            int start=1;
            /* parse flags */
            while(start<ntoks && toks[start][0]=='-' && toks[start][1]){
                for(int fi=1;toks[start][fi];fi++){
                    if(toks[start][fi]=='i') is_int=1;
                    else if(toks[start][fi]=='a') is_array=1;
                    else if(toks[start][fi]=='A') is_array=1;
                    else if(toks[start][fi]=='r') is_readonly=1;
                }
                start++;
            }
            /* merge name=value tokens (name= and "value" are separate tokens) */
            char merged[64][512]; int nm=0;
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
            char *mtoks[64];
            for(int j=0;j<nm;j++) mtoks[j]=merged[j];
            Node *nd=make_cmd(mtoks,nm,lineno);
            nd->type=NODE_LOCAL;
            for(int j=0;j<nd->argc;j++){
                char *eq=strchr(nd->argv[j],'=');
                if(eq){
                    *eq=0;
                    if(is_array || (eq[1]=='('))
                        add_var(nd->argv[j],V_ARRAY);
                    else if(is_int)
                        add_var(nd->argv[j],V_INT);
                    else
                        add_var(nd->argv[j],V_STR);
                    *eq='=';
                } else {
                    if(is_array) add_var(nd->argv[j],V_ARRAY);
                    else if(is_int) add_var(nd->argv[j],V_INT);
                    else add_var(nd->argv[j],V_STR);
                }
            }
            parser_append(nd); return;
        }
        if(!strcmp(kw,"trap")){
            Node *nd=new_node(NODE_TRAP,lineno);
            if(ntoks>=3){ nd->trap_action=xstrdup(toks[1]); nd->trap_sig=atoi(toks[2]); }
            else if(ntoks>=2){ nd->trap_action=xstrdup(toks[1]); nd->trap_sig=0; }
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
            int is_append=0;
            char *eq=strstr(toks[0],"+=");
            if(eq){ is_append=1; *eq=0; eq++; } /* eq now points to = */
            else { eq=strchr(toks[0],'='); *eq=0; }
            Node *nd=new_node(NODE_ASSIGN,lineno);
            nd->lhs=xstrdup(toks[0]);
            if(is_append){
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
                    /* string/numeric append */
                    add_var(nd->lhs,V_STR);
                    nd->rhs=xstrdup(rhs);
                    nd->lineno = -2; /* signal string append */
                }
                parser_append(nd); return;
            }
            char rhs[4096]=""; strncpy(rhs,eq+1,4095);
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
            nd->left=make_cmd(toks+1,ntoks-1,lineno);
            parser_append(nd); return;
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
    char line[8192]; int lineno=0;
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
                        if(line[i]=='"') in_dquote=1;
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
                            if(di2>0){
                                /* Read heredoc body and append to current line */
                                char hdline2[2048];
                                int cur_len = strlen(t);
                                while(fgets(hdline2,sizeof(hdline2),f)){
                                    lineno++;
                                    hdline2[strcspn(hdline2,"\r\n")]=0;
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
                            char hdtext[16384]=""; char hdline[2048];
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
                                strncat(hdtext,hdline,sizeof(hdtext)-strlen(hdtext)-2);
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
        char linecopy[8192]; strncpy(linecopy,t,8191); linecopy[8191]=0;
        char *toks[512]; int ntoks=tokenize(linecopy,toks,512);
        if(!ntoks) continue;
        /* expand brace patterns */
        ntoks=expand_braces(toks,ntoks,512);
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

/* ================================================================== */
/* L10 Runtime library (emitted into output C file)                  */
/* ================================================================== */

const char *RT_HEADER =
"/* ---- shell2c runtime (deep optimization) ---- */\n"
"#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
"#include <ctype.h>\n#include <unistd.h>\n#include <dirent.h>\n"
"#include <fcntl.h>\n#include <sys/stat.h>\n#include <sys/wait.h>\n"
"#include <sys/types.h>\n#include <errno.h>\n#include <time.h>\n"
"#include <pwd.h>\n#include <grp.h>\n#include <signal.h>\n#include <regex.h>\n"
"#include <sys/utsname.h>\n#include <sys/ioctl.h>\n#include <termios.h>\n"
"#include <glob.h>\n#include <fnmatch.h>\n#include <stdarg.h>\n"
"\n"
"static int __exit_status=0;\n"
"static int __sh_argc=0;\n"
"static int __sh_last_bg_pid=0;\n"
"static int __sh_set_e=0;\n"
"static int __sh_set_u=0;\n"
"static int __sh_set_x=0;\n"
"static char __sh_indirect_buf[1024];\n"
"static char __sh_substr_buf[4096];\n"
"static char __sh_strip_buf[4096];\n"
"static char __sh_replace_buf[4096];\n"
"static char __sh_upper_buf[4096];\n"
"static char __sh_lower_buf[4096];\n"
"static char __sh_cmd_out_buf[65536];\n"
"\n"
"/* ---- string helpers ---- */\n"
"static const char *__sh_indirect(const char *name){\n"
"  char vn[256]; snprintf(vn,sizeof(vn),\"%s\",name);\n"
"  /* look up env */\n"
"  const char *v=getenv(vn); if(v) return v;\n"
"  return \"\";\n"
"}\n"
"static const char *__sh_getenv(const char *name){\n"
"  const char *v=getenv(name); return v?v:\"\";\n"
"}\n"
"static char __sh_arr_buf[8192];\n"
"static const char *__sh_arr_join(const char **arr){\n"
"  __sh_arr_buf[0]=0;\n"
"  for(int i=0;arr[i];i++){ if(i>0) strncat(__sh_arr_buf,\" \",sizeof(__sh_arr_buf)-strlen(__sh_arr_buf)-1); strncat(__sh_arr_buf,arr[i]?arr[i]:\"\",sizeof(__sh_arr_buf)-strlen(__sh_arr_buf)-1); }\n"
"  return __sh_arr_buf;\n"
"}\n"
"static int __sh_arr_count(const char **arr){\n"
"  int n=0; while(arr[n]) n++; return n;\n"
"}\n"
"static long __sh_pow(long base,long exp){\n"
"  if(exp<0) return 0;\n"
"  long r=1; while(exp-->0) r*=base; return r;\n"
"}\n"
"static const char *__sh_arr_slice(const char **arr,int off,int len){\n"
"  int n=0; while(arr[n]) n++;\n"
"  if(off<0) off+=n; if(off<0) off=0; if(off>n) off=n;\n"
"  int end=(len<0)?n:off+len; if(end>n) end=n;\n"
"  __sh_arr_buf[0]=0;\n"
"  for(int i=off;i<end;i++){ if(i>off) strncat(__sh_arr_buf,\" \",sizeof(__sh_arr_buf)-strlen(__sh_arr_buf)-1); strncat(__sh_arr_buf,arr[i]?arr[i]:\"\",sizeof(__sh_arr_buf)-strlen(__sh_arr_buf)-1); }\n"
"  return __sh_arr_buf;\n"
"}\n"
"static const char *__sh_substr(const char *s,int off,int len){\n"
"  int n=(int)strlen(s);\n"
"  if(off<0) off+=n; if(off<0) off=0; if(off>n) off=n;\n"
"  int end = (len<0)? n : off+len; if(end>n) end=n;\n"
"  int l=end-off; if(l<0) l=0; if(l>=(int)sizeof(__sh_substr_buf)) l=(int)sizeof(__sh_substr_buf)-1;\n"
"  memcpy(__sh_substr_buf,s+off,l); __sh_substr_buf[l]=0;\n"
"  return __sh_substr_buf;\n"
"}\n"
"static const char *__sh_strip_prefix(const char *s,const char *pat,int greedy){\n"
"  /* simple glob via fnmatch on prefixes */\n"
"  int best=0; int n=(int)strlen(s);\n"
"  if(greedy){ for(int i=n;i>=1;i--){ char tmp[1024]; int l=i; if(l>=(int)sizeof(tmp)) continue; memcpy(tmp,s,l); tmp[l]=0; if(fnmatch(pat,tmp,0)==0){ best=i; break; } } }\n"
"  else { for(int i=1;i<=n;i++){ char tmp[1024]; int l=i; if(l>=(int)sizeof(tmp)) continue; memcpy(tmp,s,l); tmp[l]=0; if(fnmatch(pat,tmp,0)==0){ best=i; break; } } }\n"
"  strncpy(__sh_strip_buf,s+best,sizeof(__sh_strip_buf)-1); __sh_strip_buf[sizeof(__sh_strip_buf)-1]=0;\n"
"  return __sh_strip_buf;\n"
"}\n"
"static const char *__sh_strip_suffix(const char *s,const char *pat,int greedy){\n"
"  int n=(int)strlen(s); int best=n;\n"
"  /* %pat: shortest suffix (largest i); %%pat: longest suffix (smallest i) */\n"
"  if(!greedy){ for(int i=n;i>=1;i--){ if(fnmatch(pat,s+i,0)==0){ best=i; break; } } }\n"
"  else { for(int i=1;i<=n;i++){ if(fnmatch(pat,s+i,0)==0){ best=i; break; } } }\n"
"  int l=best; if(l<0) l=0; if(l>=(int)sizeof(__sh_strip_buf)) l=(int)sizeof(__sh_strip_buf)-1;\n"
"  memcpy(__sh_strip_buf,s,l); __sh_strip_buf[l]=0;\n"
"  return __sh_strip_buf;\n"
"}\n"
"static const char *__sh_replace(const char *s,const char *old,const char *newp,int global,int anchor_start,int anchor_end){\n"
"  char *out=__sh_replace_buf; int oi=0; int sl=(int)strlen(s); int ol=(int)strlen(old); int nl=(int)strlen(newp);\n"
"  int i=0; int done=0;\n"
"  while(i<sl && oi<(int)sizeof(__sh_replace_buf)-nl-2){\n"
"    if(!done && (anchor_start?i==0:1) && strncmp(s+i,old,ol)==0){\n"
"      memcpy(out+oi,newp,nl); oi+=nl; i+=ol; if(!global){ done=1; }\n"
"      if(anchor_start) anchor_start=0;\n"
"    } else { out[oi++]=s[i++]; }\n"
"  }\n"
"  out[oi]=0;\n"
"  return out;\n"
"}\n"
"static const char *__sh_upper(const char *s){\n"
"  int i; for(i=0;s[i]&&i<(int)sizeof(__sh_upper_buf)-1;i++) __sh_upper_buf[i]=toupper((unsigned char)s[i]); __sh_upper_buf[i]=0; return __sh_upper_buf;\n"
"}\n"
"static const char *__sh_lower(const char *s){\n"
"  int i; for(i=0;s[i]&&i<(int)sizeof(__sh_lower_buf)-1;i++) __sh_lower_buf[i]=tolower((unsigned char)s[i]); __sh_lower_buf[i]=0; return __sh_lower_buf;\n"
"}\n"
"static const char *__sh_cmd_output(const char *cmd){\n"
"  FILE *p=popen(cmd,\"r\"); if(!p){ return \"\"; }\n"
"  size_t n=fread(__sh_cmd_out_buf,1,sizeof(__sh_cmd_out_buf)-1,p); pclose(p);\n"
"  if(n>0 && __sh_cmd_out_buf[n-1]=='\\n') n--;\n"
"  __sh_cmd_out_buf[n]=0;\n"
"  return __sh_cmd_out_buf;\n"
"}\n"
"static void __sh_usleep(unsigned us){ usleep(us); }\n"
"static char __sh_fmt_buf[8192];\n"
"static const char *__sh_fmt(const char *fmt, ...){\n"
"  va_list ap; va_start(ap, fmt);\n"
"  vsnprintf(__sh_fmt_buf, sizeof(__sh_fmt_buf), fmt, ap);\n"
"  va_end(ap); return __sh_fmt_buf;\n"
"}\n"
"static char __sh_fn_out_buf[65536];\n"
"static const char *__sh_capture_fn(void (*fn)(int,char**), int argc, char **argv){\n"
"  int pfd[2]; if(pipe(pfd)<0){ return \"\"; }\n"
"  pid_t pid=fork();\n"
"  if(pid==0){\n"
"    close(pfd[0]); dup2(pfd[1],1); close(pfd[1]);\n"
"    setvbuf(stdout,NULL,_IONBF,0);\n"
"    fn(argc,argv);\n"
"    fflush(stdout); _exit(0);\n"
"  }\n"
"  close(pfd[1]);\n"
"  size_t n=fread(__sh_fn_out_buf,1,sizeof(__sh_fn_out_buf)-1,fdopen(pfd[0],\"r\"));\n"
"  waitpid(pid,NULL,0);\n"
"  if(n>0 && __sh_fn_out_buf[n-1]=='\\n') n--;\n"
"  __sh_fn_out_buf[n]=0;\n"
"  return __sh_fn_out_buf;\n"
"}\n"
"/* Process substitution: <(cmd) or >(cmd).\n"
" * Returns a /dev/fd/N path string. For <(cmd), cmd's stdout is connected\n"
" * to the read end. For >(cmd), cmd's stdin is connected to the write end. */\n"
"static char __sh_ps_buf[64];\n"
"static const char *__sh_proc_subst(const char *cmd, char dir){\n"
"  int pfd[2]; if(pipe(pfd)<0) return \"/dev/null\";\n"
"  pid_t pid=fork();\n"
"  if(pid==0){\n"
"    if(dir=='<'){ close(pfd[0]); dup2(pfd[1],1); close(pfd[1]); }\n"
"    else { close(pfd[1]); dup2(pfd[0],0); close(pfd[0]); }\n"
"    execl(\"/bin/sh\",\"sh\",\"-c\",cmd,(char*)NULL); _exit(127);\n"
"  }\n"
"  if(dir=='<'){ close(pfd[1]); snprintf(__sh_ps_buf,sizeof(__sh_ps_buf),\"/dev/fd/%d\",pfd[0]); }\n"
"  else { close(pfd[0]); snprintf(__sh_ps_buf,sizeof(__sh_ps_buf),\"/dev/fd/%d\",pfd[1]); }\n"
"  return __sh_ps_buf;\n"
"}\n"
"static void __sh_echo_escape(const char *s){\n"
"  while(*s){ if(*s=='\\\\' && *(s+1)){ s++; switch(*s){ case 'n':putchar('\\n');break; case 't':putchar('\\t');break; case 'r':putchar('\\r');break; case '\\\\':putchar('\\\\');break; case '0':putchar('\\0');break; default:putchar(*s);break; } s++; } else putchar(*s++); }\n"
"}\n"
"static void __sh_printf(const char *fmt,...){\n"
"  /* Shell-style printf: all args are strings, %d/%i convert via atoi */\n"
"  va_list ap; va_start(ap,fmt);\n"
"  const char *p=fmt;\n"
"  while(*p){\n"
"    if(*p=='%' && *(p+1)){\n"
"      p++;\n"
"      if(*p=='d'||*p=='i'){ const char *s=va_arg(ap,const char*); printf(\"%d\",s?atoi(s):0); }\n"
"      else if(*p=='s'){ const char *s=va_arg(ap,const char*); fputs(s?s:\"(null)\",stdout); }\n"
"      else if(*p=='c'){ const char *s=va_arg(ap,const char*); putchar(s?s[0]:' '); }\n"
"      else if(*p=='x'){ const char *s=va_arg(ap,const char*); printf(\"%x\",s?atoi(s):0); }\n"
"      else if(*p=='X'){ const char *s=va_arg(ap,const char*); printf(\"%X\",s?atoi(s):0); }\n"
"      else if(*p=='o'){ const char *s=va_arg(ap,const char*); printf(\"%o\",s?atoi(s):0); }\n"
"      else if(*p=='%'){ putchar('%'); }\n"
"      else { putchar('%'); putchar(*p); }\n"
"    } else { putchar(*p); }\n"
"    p++;\n"
"  }\n"
"  va_end(ap); fflush(stdout);\n"
"}\n"
"/* Elegant output helpers — reduce repetitive fputs+putchar patterns */\n"
"static void __sh_puts(const char *s){ fputs(s,stdout); putchar('\\n'); }\n"
"static void __sh_putf(const char *fmt,...){\n"
"  va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap); putchar('\\n'); fflush(stdout);\n"
"}\n"
"static void __sh_arr_free(char **arr){ for(int i=0;arr[i];i++) free(arr[i]); free(arr); }\n"
"/* ---- test(1) helpers ---- */\n"
"static int __sh_test_file(const char *p,int dir){ struct stat st; if(stat(p,&st)!=0) return 0; return dir?S_ISDIR(st.st_mode):1; }\n"
"static int __sh_test_sfile(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_size>0; }\n"
"static int __sh_test_link(const char *p){ struct stat st; if(lstat(p,&st)!=0) return 0; return S_ISLNK(st.st_mode); }\n"
"static int __sh_test_fifo(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISFIFO(st.st_mode); }\n"
"static int __sh_test_sock(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISSOCK(st.st_mode); }\n"
"static int __sh_test_blk(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISBLK(st.st_mode); }\n"
"static int __sh_test_chr(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISCHR(st.st_mode); }\n"
"static int __sh_test_mode(const char *p,int mask){ struct stat st; if(stat(p,&st)!=0) return 0; return (st.st_mode&mask)!=0; }\n"
"static int __sh_test_var(const char *name){ return getenv(name)!=NULL; }\n"
"static int __sh_test_owner(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_uid==getuid(); }\n"
"static int __sh_test_group(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_gid==getgid(); }\n"
"static int __sh_test_newer(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_mtime>st.st_atime; }\n"
"static int __sh_test_same(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0||stat(b,&sb)!=0) return 0; return sa.st_dev==sb.st_dev&&sa.st_ino==sb.st_ino; }\n"
"static int __sh_test_nt(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0) return 0; if(stat(b,&sb)!=0) return 1; return sa.st_mtime>sb.st_mtime; }\n"
"static int __sh_test_ot(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0) return 0; if(stat(b,&sb)!=0) return 0; return sa.st_mtime<sb.st_mtime; }\n"
"static int __sh_test_cmd(const char *cmd){ FILE *p=popen(cmd,\"r\"); if(!p) return 0; char buf[1024]; int got=0; while(fgets(buf,sizeof(buf),p)){ got=1; } pclose(p); return got; }\n"
"static int __sh_regex(const char *s,const char *pat){ regex_t r; if(regcomp(&r,pat,REG_EXTENDED|REG_NOSUB)!=0) return 0; int rc=regexec(&r,s,0,NULL,0); regfree(&r); return rc==0; }\n"
"\n"
"/* ---- file builtins ---- */\n"
"static void __attribute__((unused)) __b_pwd(void){ char b[4096]; if(getcwd(b,sizeof(b)))printf(\"%s\\n\",b); else perror(\"pwd\"); }\n"
"static void __attribute__((unused)) __b_ls(const char *flags,...){\n"
"  va_list ap; va_start(ap,flags);\n"
"  int show_all=0,long_fmt=0,rev=0;\n"
"  for(const char *f=flags;f&&*f;f++){ if(*f=='a')show_all=1; if(*f=='l')long_fmt=1; if(*f=='r')rev=1; }\n"
"  const char *path; int first=1;\n"
"  while((path=va_arg(ap,const char*))!=NULL){\n"
"    DIR *dp=opendir(path[0]?path:\".\"); if(!dp){perror(path);continue;}\n"
"    if(!first) printf(\"\\n\"); first=0;\n"
"    struct dirent *e; struct stat st; char full[4096];\n"
"    while((e=readdir(dp))){ if(!show_all&&e->d_name[0]=='.') continue;\n"
"      if(long_fmt){ snprintf(full,sizeof(full),\"%s/%s\",path,e->d_name); if(stat(full,&st)==0){ printf(\"%c %8ld %s\\n\",S_ISDIR(st.st_mode)?'d':'-',(long)st.st_size,e->d_name); } else printf(\"? %8s %s\\n\",\"?\",e->d_name); }\n"
"      else printf(\"%s\\n\",e->d_name);\n"
"    }\n"
"    closedir(dp);\n"
"  }\n"
"  if(first){ DIR *dp=opendir(\".\"); if(dp){ struct dirent *e; while((e=readdir(dp)))if(show_all||e->d_name[0]!='.')printf(\"%s\\n\",e->d_name); closedir(dp);} }\n"
"  va_end(ap);\n"
"}\n"
"static int __attribute__((unused)) __b_cp(const char *s,const char *d){\n"
"  struct stat st; if(stat(s,&st)==0 && S_ISDIR(st.st_mode)){ char cmd[8192]; snprintf(cmd,sizeof(cmd),\"cp -r \\\"%s\\\" \\\"%s\\\"\",s,d); return system(cmd); }\n"
"  int f1=open(s,O_RDONLY); if(f1<0){perror(\"cp\");return-1;}\n"
"  int f2=open(d,O_WRONLY|O_CREAT|O_TRUNC,0644); if(f2<0){perror(\"cp\");close(f1);return-1;}\n"
"  char buf[65536]; ssize_t r;\n"
"  while((r=read(f1,buf,sizeof(buf)))>0){ ssize_t w=0;while(w<r){ssize_t x=write(f2,buf+w,r-w);if(x<0){perror(\"cp\");close(f1);close(f2);return-1;}w+=x;}}\n"
"  close(f1);close(f2);return 0;\n"
"}\n"
"static int __attribute__((unused)) __b_mv(const char *s,const char *d){if(rename(s,d)!=0){perror(\"mv\");return-1;}return 0;}\n"
"static int __attribute__((unused)) __b_rm(const char *p,int recursive){\n"
"  struct stat st; if(stat(p,&st)!=0){perror(\"rm\");return-1;}\n"
"  if(S_ISDIR(st.st_mode)){ if(recursive){ char cmd[8192]; snprintf(cmd,sizeof(cmd),\"rm -rf \\\"%s\\\"\",p); return system(cmd); } else { if(rmdir(p)!=0){perror(\"rm\");return-1;} } }\n"
"  else { if(unlink(p)!=0){perror(\"rm\");return-1;} }\n"
"  return 0;\n"
"}\n"
"static int __attribute__((unused)) __b_mkdir(const char *p,int parents){\n"
"  if(parents){ char tmp[1024]; strncpy(tmp,p,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0; int l=(int)strlen(tmp);\n"
"    for(int i=1;i<l;i++){ if(tmp[i]=='/'){ tmp[i]=0; mkdir(tmp,0755); tmp[i]='/'; } }\n"
"    return mkdir(tmp,0755);\n"
"  }\n"
"  if(mkdir(p,0755)!=0){perror(\"mkdir\");return-1;} return 0;\n"
"}\n"
"static int __attribute__((unused)) __b_rmdir(const char *p){if(rmdir(p)!=0){perror(\"rmdir\");return-1;}return 0;}\n"
"static void __attribute__((unused)) __b_touch(const char *p){ int fd=open(p,O_WRONLY|O_CREAT,0644);if(fd>=0)close(fd);else perror(\"touch\");}\n"
"static int __attribute__((unused)) __b_ln(const char *a,const char *b,int sym){ if(sym){ if(symlink(a,b)!=0){perror(\"ln\");return-1;} } else { if(link(a,b)!=0){perror(\"ln\");return-1;} } return 0; }\n"
"static int __attribute__((unused)) __b_chmod(const char *mode,const char *p){ int m=(int)strtol(mode,NULL,8); if(chmod(p,m)!=0){perror(\"chmod\");return-1;} return 0; }\n"
"static int __attribute__((unused)) __b_chown(const char *owner,const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),\"chown %s \\\"%s\\\"\",owner,p); return system(cmd); }\n"
"static void __attribute__((unused)) __b_stat(const char *p){ struct stat st; if(stat(p,&st)!=0){perror(\"stat\");return;} printf(\"  File: %s\\n  Size: %ld\\n\",p,(long)st.st_size); printf(\"  Mode: %o\\n\",st.st_mode&0777); printf(\"  Uid: %d  Gid: %d\\n\",st.st_uid,st.st_gid); }\n"
"static void __attribute__((unused)) __b_du(const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),\"du -sh \\\"%s\\\" 2>/dev/null\",p); system(cmd); }\n"
"static void __attribute__((unused)) __b_df(void){ system(\"df -h\"); }\n"
"static void __attribute__((unused)) __b_file(const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),\"file \\\"%s\\\"\",p); system(cmd); }\n"
"static void __attribute__((unused)) __b_basename(const char *p){ const char *b=strrchr(p,'/'); printf(\"%s\\n\",b?b+1:p); }\n"
"static void __attribute__((unused)) __b_dirname(const char *p){ char tmp[1024]; strncpy(tmp,p,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0; char *sl=strrchr(tmp,'/'); if(sl){ *sl=0; printf(\"%s\\n\",tmp[0]?tmp:\"/\"); } else printf(\".\\n\"); }\n"
"static void __attribute__((unused)) __b_realpath(const char *p){ char *r=realpath(p,NULL); if(r){ printf(\"%s\\n\",r); free(r); } else perror(\"realpath\"); }\n"
"static void __attribute__((unused)) __b_readlink(const char *p){ char buf[4096]; ssize_t n=readlink(p,buf,sizeof(buf)-1); if(n<0) perror(\"readlink\"); else { buf[n]=0; printf(\"%s\\n\",buf); } }\n"
"static void __attribute__((unused)) __b_mktemp(void){ char t[]=\"/tmp/sh2cXXXXXX\"; int fd=mkstemp(t); if(fd>=0){ printf(\"%s\\n\",t); close(fd); } }\n"
"static int __attribute__((unused)) __b_install(const char *s,const char *d){ return __b_cp(s,d); }\n"
"\n"
"/* ---- text builtins ---- */\n"
"static void __attribute__((unused)) __b_cat(const char *path,int flags){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"cat\");return;}\n"
"  char ln[8192]; int n=1;\n"
"  while(fgets(ln,sizeof(ln),f)){ if(flags&1) printf(\"%6d  %s\",n,ln); else fputs(ln,stdout); if(flags&2){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\\n') ln[l-1]='$'; } n++; }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_grep(const char *pat,const char *path,const char *flags){\n"
"  int ignore_case=(flags&&strchr(flags,'i'))?REG_ICASE:0;\n"
"  int invert=(flags&&strchr(flags,'v'))?1:0;\n"
"  int line_no=(flags&&strchr(flags,'n'))?1:0;\n"
"  regex_t r; if(regcomp(&r,pat,REG_EXTENDED|ignore_case)!=0){ fprintf(stderr,\"grep: bad pattern\\n\"); return; }\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"grep\");regfree(&r);return;}\n"
"  char ln[8192]; int n=1;\n"
"  while(fgets(ln,sizeof(ln),f)){ int m=regexec(&r,ln,0,NULL,0)==0; if(m!=invert){ if(line_no) printf(\"%d:\",n); fputs(ln,stdout); } n++; }\n"
"  if(path)fclose(f); regfree(&r);\n"
"}\n"
"static void __attribute__((unused)) __b_sed(const char *script,const char *path){\n"
"  /* support s/old/new/[g] and s|old|new|[g] */\n"
"  if(script[0]!='s'){ if(path){ char cmd[8192]; snprintf(cmd,sizeof(cmd),\"sed '%s' \\\"%s\\\"\",script,path); system(cmd); } else { char cmd[8192]; snprintf(cmd,sizeof(cmd),\"sed '%s'\",script); system(cmd); } return; }\n"
"  char delim=script[1]; const char *p=script+2; char old[1024]; int oi=0;\n"
"  while(*p && *p!=delim && oi<(int)sizeof(old)-1) old[oi++]=*p++; old[oi]=0; if(*p) p++;\n"
"  char newp[1024]; int ni=0;\n"
"  while(*p && *p!=delim && ni<(int)sizeof(newp)-1) newp[ni++]=*p++; newp[ni]=0; if(*p) p++;\n"
"  int global=0; while(*p){ if(*p=='g') global=1; p++; }\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"sed\");return;}\n"
"  char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),f)){ char out[16384]; int oi2=0; int i=0; int ol=(int)strlen(old); int nl=(int)strlen(newp); int sl=(int)strlen(ln); int done=0;\n"
"    while(i<sl && oi2<(int)sizeof(out)-nl-2){ if(!done && ol>0 && strncmp(ln+i,old,ol)==0){ memcpy(out+oi2,newp,nl); oi2+=nl; i+=ol; if(!global) done=1; } else out[oi2++]=ln[i++]; }\n"
"    while(i<sl && oi2<(int)sizeof(out)-1) out[oi2++]=ln[i++];\n"
"    out[oi2]=0; fputs(out,stdout);\n"
"  }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_tr(const char *s1,const char *s2){\n"
"  /* Expand ranges like a-z, A-Z, 0-9 in s1 and s2 */\n"
"  char exp1[256], exp2[256]; int e1=0, e2=0;\n"
"  for(int i=0;s1[i]&&e1<255;i++){ if(s1[i]=='-'&&i>0&&s1[i+1]&&e1>0){ char lo=exp1[e1-1], hi=s1[i+1]; for(char c=lo+1;c<=hi&&e1<255;c++) exp1[e1++]=c; i++; } else exp1[e1++]=s1[i]; }\n"
"  exp1[e1]=0;\n"
"  for(int i=0;s2[i]&&e2<255;i++){ if(s2[i]=='-'&&i>0&&s2[i+1]&&e2>0){ char lo=exp2[e2-1], hi=s2[i+1]; for(char c=lo+1;c<=hi&&e2<255;c++) exp2[e2++]=c; i++; } else exp2[e2++]=s2[i]; }\n"
"  exp2[e2]=0;\n"
"  int c; int l2=e2;\n"
"  while((c=fgetc(stdin))!=EOF){\n"
"    const char *p=strchr(exp1,c);\n"
"    if(p){ int idx=(int)(p-exp1); putchar(idx<l2?exp2[idx]:(l2>0?exp2[l2-1]:c)); }\n"
"    else putchar(c);\n"
"  }\n"
"}\n"
"static void __attribute__((unused)) __b_cut(const char *fields,char delim,const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"cut\");return;}\n"
"  char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),f)){\n"
"    int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\\n') ln[--l]=0;\n"
"    /* parse fields like 1,3 or 1-3 */\n"
"    char fc[256]; strncpy(fc,fields,sizeof(fc)-1); fc[sizeof(fc)-1]=0;\n"
"    char *tok=strtok(fc,\",\"); int first=1;\n"
"    while(tok){ int a,b; if(strchr(tok,'-')){ sscanf(tok,\"%d-%d\",&a,&b); } else { a=b=atoi(tok); }\n"
"      int col=1; char *p=ln; char *start=p;\n"
"      while(*p){ if(*p==delim){ if(col>=a&&col<=b){ if(!first)putchar(delim); int len=(int)(p-start); printf(\"%.*s\",len,start); first=0; } col++; start=p+1; } p++; }\n"
"      if(col>=a&&col<=b){ if(!first)putchar(delim); fputs(start,stdout); first=0; }\n"
"      tok=strtok(NULL,\",\");\n"
"    }\n"
"    putchar('\\n');\n"
"  }\n"
"  if(path)fclose(f);\n"
"}\n"
"static int __sort_cmp(const void *a,const void *b){ return strcmp(*(const char**)a,*(const char**)b); }\n"
"static int __sort_cmp_rev(const void *a,const void *b){ return strcmp(*(const char**)b,*(const char**)a); }\n"
"static int __sort_cmp_num(const void *a,const void *b){ double da=atof(*(const char**)a),db=atof(*(const char**)b); return (da>db)-(da<db); }\n"
"static void __attribute__((unused)) __b_sort(const char *path,int rev,int uniq,int num){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"sort\");return;}\n"
"  char **lines=NULL; int n=0,cap=0; char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){ cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*)); } lines[n++]=strdup(ln); }\n"
"  if(path)fclose(f);\n"
"  if(num) qsort(lines,n,sizeof(char*),rev?__sort_cmp_rev:__sort_cmp_num);\n"
"  else qsort(lines,n,sizeof(char*),rev?__sort_cmp_rev:__sort_cmp);\n"
"  const char *prev=NULL;\n"
"  for(int i=0;i<n;i++){ if(uniq&&prev&&strcmp(prev,lines[i])==0){ free(lines[i]); continue; } fputs(lines[i],stdout); prev=lines[i]; }\n"
"  for(int i=0;i<n;i++) free(lines[i]); free(lines);\n"
"}\n"
"static void __attribute__((unused)) __b_uniq(const char *path,int count){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"uniq\");return;}\n"
"  char ln[8192]; char prev[8192]=\"\"; int cnt=0,first=1;\n"
"  while(fgets(ln,sizeof(ln),f)){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\\n') ln[--l]=0;\n"
"    if(first||strcmp(prev,ln)!=0){ if(!first){ if(count) printf(\"%7d %s\\n\",cnt,prev); else printf(\"%s\\n\",prev); } strcpy(prev,ln); cnt=1; first=0; }\n"
"    else cnt++;\n"
"  }\n"
"  if(!first){ if(count) printf(\"%7d %s\\n\",cnt,prev); else printf(\"%s\\n\",prev); }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_wc(const char *path,int dl,int dw,int dc){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"wc\");return;}\n"
"  long li=0,wi=0,ci=0; int iw=0,c;\n"
"  while((c=fgetc(f))!=EOF){ci++;if(c=='\\n')li++;if(isspace(c))iw=0;else if(!iw){iw=1;wi++;}}\n"
"  if(path)fclose(f);\n"
"  {int __w1=1;\n"
"  if(dl)printf(\"%s%ld\",__w1?\"\":\" \",li),__w1=0;if(dw)printf(\"%s%ld\",__w1?\"\":\" \",wi),__w1=0;if(dc)printf(\"%s%ld\",__w1?\"\":\" \",ci),__w1=0;\n"
"  if(path)printf(\"%s%s\",__w1?\"\":\" \",path);putchar('\\n');\n"
"  }\n"
"}\n"
"static void __attribute__((unused)) __b_head(const char *path,int n){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"head\");return;}\n"
"  char ln[8192]; int c=0; while(c<n&&fgets(ln,sizeof(ln),f)){fputs(ln,stdout);c++;}\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_tail(const char *path,int n){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"tail\");return;}\n"
"  char **buf=(char**)malloc(n*sizeof(char*)); int i; for(i=0;i<n;i++)buf[i]=NULL;\n"
"  char ln[8192]; int idx=0,tot=0;\n"
"  while(fgets(ln,sizeof(ln),f)){free(buf[idx%n]);buf[idx%n]=strdup(ln);idx++;tot++;}\n"
"  if(path)fclose(f);\n"
"  int st=(tot>n)?idx%n:0,cnt=(tot>n)?n:tot;\n"
"  for(i=0;i<cnt;i++){int j=(st+i)%n;if(buf[j])fputs(buf[j],stdout);}\n"
"  for(i=0;i<n;i++)free(buf[i]);free(buf);\n"
"}\n"
"static void __attribute__((unused)) __b_tee(int append,...){\n"
"  va_list ap; va_start(ap,append);\n"
"  FILE **fps=NULL; int nf=0;\n"
"  const char *p;\n"
"  while((p=va_arg(ap,const char*))!=NULL){ fps=realloc(fps,(nf+1)*sizeof(FILE*)); fps[nf]=fopen(p,append?\"a\":\"w\"); nf++; }\n"
"  va_end(ap);\n"
"  int c; while((c=fgetc(stdin))!=EOF){ putchar(c); for(int i=0;i<nf;i++) if(fps[i]) fputc(c,fps[i]); }\n"
"  for(int i=0;i<nf;i++) if(fps[i]) fclose(fps[i]); free(fps);\n"
"}\n"
"static void __attribute__((unused)) __b_xargs(const char *cmd,...){\n"
"  va_list ap; va_start(ap,cmd);\n"
"  const char *extra[32]; int ne=0; const char *p;\n"
"  while((p=va_arg(ap,const char*))!=NULL && ne<31) extra[ne++]=p;\n"
"  va_end(ap);\n"
"  char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),stdin)){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\\n') ln[--l]=0;\n"
"    char full[16384]; snprintf(full,sizeof(full),\"%s\",cmd);\n"
"    for(int i=0;i<ne;i++){ strncat(full,\" \",sizeof(full)-strlen(full)-1); strncat(full,extra[i],sizeof(full)-strlen(full)-1); }\n"
"    strncat(full,\" \",sizeof(full)-strlen(full)-1); strncat(full,ln,sizeof(full)-strlen(full)-1);\n"
"    system(full);\n"
"  }\n"
"}\n"
"static void __attribute__((unused)) __b_rev(const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"rev\");return;}\n"
"  char ln[8192]; while(fgets(ln,sizeof(ln),f)){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\\n') ln[--l]=0; for(int i=l-1;i>=0;i--) putchar(ln[i]); putchar('\\n'); }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_tac(const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"tac\");return;}\n"
"  char **lines=NULL; int n=0,cap=0; char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));} lines[n++]=strdup(ln); }\n"
"  if(path)fclose(f);\n"
"  for(int i=n-1;i>=0;i--){ fputs(lines[i],stdout); free(lines[i]); }\n"
"  free(lines);\n"
"}\n"
"static void __attribute__((unused)) __b_nl(const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"nl\");return;}\n"
"  char ln[8192]; int n=1; while(fgets(ln,sizeof(ln),f)){ printf(\"%6d  %s\",n,ln); n++; }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_fold(const char *path,int w){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"fold\");return;}\n"
"  int c,col=0; while((c=fgetc(f))!=EOF){ putchar(c); if(c=='\\n') col=0; else { col++; if(col>=w){ putchar('\\n'); col=0; } } }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_paste(const char *first,...){\n"
"  va_list ap; va_start(ap,first);\n"
"  FILE *fps[16]; const char *names[16]; int nf=0;\n"
"  if(first){ names[nf]=first; fps[nf]=fopen(first,\"r\"); nf++; }\n"
"  const char *p; while((p=va_arg(ap,const char*))!=NULL && nf<16){ names[nf]=p; fps[nf]=fopen(p,\"r\"); nf++; }\n"
"  va_end(ap);\n"
"  if(nf==0) return;\n"
"  char lns[16][8192]; int alive=1;\n"
"  while(alive){ alive=0; for(int i=0;i<nf;i++){ if(fps[i]&&fgets(lns[i],sizeof(lns[i]),fps[i])){ alive=1; int l=(int)strlen(lns[i]); if(l>0&&lns[i][l-1]=='\\n') lns[i][--l]=0; } else lns[i][0]=0; if(i>0) putchar('\\t'); fputs(lns[i],stdout); } putchar('\\n'); }\n"
"  for(int i=0;i<nf;i++) if(fps[i]) fclose(fps[i]);\n"
"}\n"
"static void __attribute__((unused)) __b_expand(const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"expand\");return;}\n"
"  int c,col=0; while((c=fgetc(f))!=EOF){ if(c=='\\t'){ do{ putchar(' '); col++; }while(col%8); } else { putchar(c); if(c=='\\n') col=0; else col++; } }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_unexpand(const char *path){\n"
"  FILE *f=path?fopen(path,\"r\"):stdin; if(!f){perror(\"unexpand\");return;}\n"
"  int c,col=0,spaces=0; while((c=fgetc(f))!=EOF){ if(c==' '){ spaces++; col++; if(col%8==0){ putchar('\\t'); spaces=0; } } else { while(spaces>0){ putchar(' '); spaces--; } putchar(c); if(c=='\\n') col=0; else col++; } }\n"
"  if(path)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_column(const char *first,...){\n"
"  va_list ap; va_start(ap,first); const char *p; FILE *f=first?fopen(first,\"r\"):stdin; while((p=va_arg(ap,const char*))!=NULL){} va_end(ap);\n"
"  if(!f){perror(\"column\");return;}\n"
"  char ln[8192]; while(fgets(ln,sizeof(ln),f)){ fputs(ln,stdout); }\n"
"  if(first)fclose(f);\n"
"}\n"
"static void __attribute__((unused)) __b_shuf(const char *first,...){\n"
"  va_list ap; va_start(ap,first); const char *p; FILE *f=first?fopen(first,\"r\"):stdin; while((p=va_arg(ap,const char*))!=NULL){} va_end(ap);\n"
"  if(!f){perror(\"shuf\");return;}\n"
"  char **lines=NULL; int n=0,cap=0; char ln[8192];\n"
"  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));} lines[n++]=strdup(ln); }\n"
"  if(first)fclose(f);\n"
"  srand((unsigned)time(NULL));\n"
"  for(int i=n-1;i>0;i--){ int j=rand()%(i+1); char *t=lines[i]; lines[i]=lines[j]; lines[j]=t; }\n"
"  for(int i=0;i<n;i++){ fputs(lines[i],stdout); free(lines[i]); } free(lines);\n"
"}\n"
"static void __attribute__((unused)) __b_comm(const char *a,const char *b){\n"
"  FILE *fa=fopen(a,\"r\"),*fb=fopen(b,\"r\"); if(!fa||!fb){perror(\"comm\");return;}\n"
"  char la[8192],lb[8192]; int ha=1,hb=1;\n"
"  ha=fgets(la,sizeof(la),fa)?1:0; hb=fgets(lb,sizeof(lb),fb)?1:0;\n"
"  while(ha||hb){\n"
"    if(ha&&hb){ int l=(int)strlen(la); if(l>0&&la[l-1]=='\\n')la[--l]=0; l=(int)strlen(lb); if(l>0&&lb[l-1]=='\\n')lb[--l]=0;\n"
"      int c=strcmp(la,lb); if(c<0){ printf(\"%s\\n\",la); ha=fgets(la,sizeof(la),fa)?1:0; } else if(c>0){ printf(\"\\t%s\\n\",lb); hb=fgets(lb,sizeof(lb),fb)?1:0; } else { printf(\"\\t\\t%s\\n\",la); ha=fgets(la,sizeof(la),fa)?1:0; hb=fgets(lb,sizeof(lb),fb)?1:0; } }\n"
"    else if(ha){ fputs(la,stdout); ha=fgets(la,sizeof(la),fa)?1:0; } else { printf(\"\\t%s\",lb); hb=fgets(lb,sizeof(lb),fb)?1:0; }\n"
"  }\n"
"  fclose(fa); fclose(fb);\n"
"}\n"
"static void __attribute__((unused)) __b_diff(const char *a,const char *b){\n"
"  FILE *fa=fopen(a,\"r\"), *fb=fopen(b,\"r\");\n"
"  if(!fa||!fb){ if(fa)fclose(fa); if(fb)fclose(fb); __exit_status=2; return; }\n"
"  int la=0,lb=0,ca,cb,diff=0;\n"
"  while(1){\n"
"    ca=fgetc(fa); cb=fgetc(fb);\n"
"    if(ca==EOF&&cb==EOF) break;\n"
"    if(ca!=cb){ diff=1; break; }\n"
"    if(ca=='\\n') la++; if(cb=='\\n') lb++;\n"
"  }\n"
"  fclose(fa); fclose(fb);\n"
"  __exit_status=diff?1:0;\n"
"}\n"
"\n"
"/* ---- search builtins ---- */\n"
"static void __attribute__((unused)) __b_find(const char *first,...){\n"
"  va_list ap; va_start(ap,first);\n"
"  char cmd[8192]=\"find\"; const char *p=first;\n"
"  while(p){ strncat(cmd,\" \",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); p=va_arg(ap,const char*); }\n"
"  va_end(ap); system(cmd);\n"
"}\n"
"static void __attribute__((unused)) __b_which(const char *cmd){ char *path=getenv(\"PATH\"); if(!path){return;} char *p=strdup(path); char *tok=strtok(p,\":\"); while(tok){ char full[4096]; snprintf(full,sizeof(full),\"%s/%s\",tok,cmd); if(access(full,X_OK)==0){ printf(\"%s\\n\",full); free(p); return; } tok=strtok(NULL,\":\"); } free(p); }\n"
"static void __attribute__((unused)) __b_whereis(const char *cmd){ char cmd2[1024]; snprintf(cmd2,sizeof(cmd2),\"whereis %s\",cmd); system(cmd2); }\n"
"static void __attribute__((unused)) __b_locate(const char *pat){ char cmd[1024]; snprintf(cmd,sizeof(cmd),\"locate %s\",pat); system(cmd); }\n"
"\n"
"/* ---- system info builtins ---- */\n"
"static void __attribute__((unused)) __b_date(const char *fmt){\n"
"  time_t t=time(NULL); struct tm *tm=localtime(&t);\n"
"  if(!fmt||!fmt[0]){ char b[256]; strftime(b,sizeof(b),\"%a %b %e %H:%M:%S %Z %Y\",tm); printf(\"%s\\n\",b); return; }\n"
"  if(fmt[0]=='+'){ char b[1024]; strftime(b,sizeof(b),fmt+1,tm); printf(\"%s\\n\",b); }\n"
"}\n"
"static void __attribute__((unused)) __b_whoami(void){ struct passwd *pw=getpwuid(getuid()); printf(\"%s\\n\",pw?pw->pw_name:\"unknown\"); }\n"
"static void __attribute__((unused)) __b_hostname(void){ char h[256]; if(gethostname(h,sizeof(h))==0) printf(\"%s\\n\",h); }\n"
"static void __attribute__((unused)) __b_hostname_set(const char *h){ if(sethostname(h,strlen(h))!=0) perror(\"hostname\"); }\n"
"static void __attribute__((unused)) __b_uname(int all){ struct utsname u; if(uname(&u)!=0){perror(\"uname\");return;} if(all) printf(\"%s %s %s %s %s\\n\",u.sysname,u.nodename,u.release,u.version,u.machine); else printf(\"%s\\n\",u.sysname); }\n"
"static void __attribute__((unused)) __b_arch(void){ struct utsname u; uname(&u); printf(\"%s\\n\",u.machine); }\n"
"static void __attribute__((unused)) __b_nproc(void){ long n=sysconf(_SC_NPROCESSORS_ONLN); printf(\"%ld\\n\",n); }\n"
"static void __attribute__((unused)) __b_id(void){ printf(\"uid=%d gid=%d\\n\",getuid(),getgid()); }\n"
"static void __attribute__((unused)) __b_env(void){ extern char **environ; for(char **e=environ;*e;e++) printf(\"%s\\n\",*e); }\n"
"static void __attribute__((unused)) __b_ps(void){ system(\"ps\"); }\n"
"static void __attribute__((unused)) __b_kill(const char *first,...){ va_list ap; va_start(ap,first); const char *p=first; char cmd[1024]=\"kill\"; while(p){ strncat(cmd,\" \",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); p=va_arg(ap,const char*); } va_end(ap); system(cmd); }\n"
"static void __attribute__((unused)) __b_wait(void){ int st; while(waitpid(-1,&st,WNOHANG)>0); }\n"
"static void __attribute__((unused)) __b_jobs(void){ /* stub */ }\n"
"static void __attribute__((unused)) __b_bg(void){ /* stub */ }\n"
"static void __attribute__((unused)) __b_fg(void){ /* stub */ }\n"
"static void __attribute__((unused)) __b_trap(const char *action,int sig){ if(action[0]==0) return; if(sig>0) signal(sig,SIG_DFL); /* simplified */ }\n"
"static void __attribute__((unused)) __b_type(const char *cmd){ printf(\"%s is a shell builtin\\n\",cmd); }\n"
"static void __attribute__((unused)) __b_command(const char *first,...){ va_list ap; va_start(ap,first); char cmd[8192]; snprintf(cmd,sizeof(cmd),\"%s\",first); const char *p; while((p=va_arg(ap,const char*))!=NULL){ strncat(cmd,\" \",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); } va_end(ap); system(cmd); }\n"
"static void __attribute__((unused)) __b_alias(const char *a){ if(!a){ system(\"alias\"); return; } }\n"
"static void __attribute__((unused)) __b_unalias(const char *a){ (void)a; }\n"
"static void __attribute__((unused)) __b_history(void){ /* stub */ }\n"
"static void __attribute__((unused)) __b_pushd(const char *d){ char cwd[4096]; getcwd(cwd,sizeof(cwd)); chdir(d); }\n"
"static void __attribute__((unused)) __b_popd(void){ /* stub */ }\n"
"static void __attribute__((unused)) __b_dirs(void){ char cwd[4096]; getcwd(cwd,sizeof(cwd)); printf(\"%s\\n\",cwd); }\n"
"static void __attribute__((unused)) __b_seq(int s,int st,int e){ if(st>0) for(int i=s;i<=e;i+=st) printf(\"%d\\n\",i); else for(int i=s;i>=e;i+=st) printf(\"%d\\n\",i); }\n"
"static void __attribute__((unused)) __b_yes(const char *msg){ while(1){ printf(\"%s\\n\",msg?msg:\"y\"); fflush(stdout); } }\n"
"static void __attribute__((unused)) __b_source(const char *f){ (void)f; }\n"
"static void __attribute__((unused)) __b_eval(const char **a){ char cmd[8192]=\"\"; for(const char **p=a;*p;p++){ if(*cmd) strncat(cmd,\" \",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,*p,sizeof(cmd)-strlen(cmd)-1); } system(cmd); }\n"
"static void __attribute__((unused)) __b_nohup(const char *first,...){ va_list ap; va_start(ap,first); char cmd[8192]; snprintf(cmd,sizeof(cmd),\"nohup %s\",first); const char *p; while((p=va_arg(ap,const char*))!=NULL){ strncat(cmd,\" \",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); } va_end(ap); strncat(cmd,\" >/dev/null 2>&1 &\",sizeof(cmd)-strlen(cmd)-1); system(cmd); }\n"
"static void __attribute__((unused)) __b_time(const char *cmd){ clock_t t0=clock(); system(cmd); clock_t t1=clock(); fprintf(stderr,\"real %.3fs\\n\",(double)(t1-t0)/CLOCKS_PER_SEC); }\n"
"static void __attribute__((unused)) __b_free_mem(void){ system(\"free\"); }\n"
"static void __attribute__((unused)) __b_uptime(void){ system(\"uptime\"); }\n"
"static void __attribute__((unused)) __b_who(void){ system(\"who\"); }\n"
"static void __attribute__((unused)) __b_last(void){ system(\"last\"); }\n"
"static void __attribute__((unused)) __b_dmesg(void){ system(\"dmesg\"); }\n"
"static void __attribute__((unused)) __b_lsof(void){ system(\"lsof\"); }\n"
"static void __attribute__((unused)) __b_mount(void){ system(\"mount\"); }\n"
"static void __attribute__((unused)) __b_tty(void){ char *t=ttyname(0); printf(\"%s\\n\",t?t:\"not a tty\"); }\n"
"static void __attribute__((unused)) __b_help(void){\n"
"  printf(\"shell2c runtime — supported builtins:\\n\");\n"
"  printf(\"  echo printf cd pwd ls cat grep sed tr cut sort uniq wc head tail\\n\");\n"
"  printf(\"  cp mv rm mkdir rmdir touch ln chmod chown stat du df file\\n\");\n"
"  printf(\"  basename dirname realpath readlink mktemp install\\n\");\n"
"  printf(\"  tee xargs rev tac nl fold paste expand unexpand column shuf comm diff\\n\");\n"
"  printf(\"  find which whereis locate\\n\");\n"
"  printf(\"  date whoami hostname uname id env export unset set source eval\\n\");\n"
"  printf(\"  ps kill sleep wait jobs bg fg trap type command alias unalias\\n\");\n"
"  printf(\"  history pushd popd dirs seq yes true false test [ expr read exit\\n\");\n"
"  printf(\"  return clear reset nohup time free uptime who last dmesg lsof\\n\");\n"
"  printf(\"  mount arch nproc tty help\\n\");\n"
"}\n"
"/* ---- end runtime ---- */\n\n";

/* ================================================================== */
/* L11 Main                                                           */
/* ================================================================== */

/* Pre-scan: walk AST and register variables from $((var=...)) in expressions */
static void prescan_register_vars(Node *n){
    while(n){
        if(n->type==NODE_CMD && n->argv){
            for(int i=0;i<n->argc;i++){
                const char *s=n->argv[i];
                if(!s) continue;
                /* scan for $(( patterns — variable assignment in arithmetic */
                const char *p=strstr(s,"$((");
                while(p){
                    p+=3;
                    /* extract variable name before = */
                    while(*p==' '||*p=='\t') p++;
                    if(isalpha((unsigned char)*p)||*p=='_'){
                        char vn[128]; int vi=0;
                        while(isalnum((unsigned char)*p)||*p=='_'){
                            if(vi<(int)sizeof(vn)-1) vn[vi++]=*p;
                            p++;
                        }
                        vn[vi]=0;
                        while(*p==' '||*p=='\t') p++;
                        if(*p=='='&&*(p+1)!='='){
                            add_var(vn,V_INT);
                        }
                    }
                    p=strstr(p,"$((");
                }
                /* scan for ${var:=...} patterns — default assignment */
                p=strstr(s,"${");
                while(p){
                    p+=2;
                    char vn[128]; int vi=0;
                    while(*p && *p!='}' && *p!=':' && *p!='#' && *p!='/' && *p!='%' && *p!='^' && *p!='[' && vi<(int)sizeof(vn)-1) vn[vi++]=*p++;
                    vn[vi]=0;
                    if(*p==':' && *(p+1)=='='){
                        if(!isdigit((unsigned char)vn[0]) && !is_known_var(vn))
                            add_var(vn,V_STR);
                    }
                    /* skip to } */
                    while(*p && *p!='}') p++;
                    if(*p=='}') p++;
                    p=strstr(p,"${");
                }
            }
        }
        /* Also scan conditions and expressions */
        if(n->cond) {
            const char *p=strstr(n->cond,"$((");
            while(p){
                p+=3;
                while(*p==' '||*p=='\t') p++;
                if(isalpha((unsigned char)*p)||*p=='_'){
                    char vn[128]; int vi=0;
                    while(isalnum((unsigned char)*p)||*p=='_'){
                        if(vi<(int)sizeof(vn)-1) vn[vi++]=*p;
                        p++;
                    }
                    vn[vi]=0;
                    while(*p==' '||*p=='\t') p++;
                    if(*p=='='&&*(p+1)!='=') add_var(vn,V_INT);
                }
                p=strstr(p,"$((");
            }
        }
        if(n->for_init){
            const char *p=n->for_init;
            while(*p){
                while(*p==' '||*p=='\t') p++;
                if(isalpha((unsigned char)*p)||*p=='_'){
                    char vn[128]; int vi=0;
                    while(isalnum((unsigned char)*p)||*p=='_'){
                        if(vi<(int)sizeof(vn)-1) vn[vi++]=*p;
                        p++;
                    }
                    vn[vi]=0;
                    while(*p==' '||*p=='\t') p++;
                    if(*p=='='&&*(p+1)!='=') add_var(vn,V_INT);
                }
                while(*p && *p!=';') p++;
                if(*p==';') p++;
            }
        }
        /* recurse into children */
        if(n->left) prescan_register_vars(n->left);
        if(n->right) prescan_register_vars(n->right);
        if(n->then_blk) prescan_register_vars(n->then_blk);
        if(n->else_blk) prescan_register_vars(n->else_blk);
        for(int i=0;i<n->elif_count;i++){
            if(n->elif_blks[i]) prescan_register_vars(n->elif_blks[i]);
        }
        if(n->body) prescan_register_vars(n->body);
        if(n->while_body) prescan_register_vars(n->while_body);
        if(n->func_body) prescan_register_vars(n->func_body);
        for(int i=0;i<n->case_count;i++){
            if(n->case_bodies[i]) prescan_register_vars(n->case_bodies[i]);
        }
        if(n->case_default) prescan_register_vars(n->case_default);
        n=n->next;
    }
}

int main(int argc, char **argv){
    int do_obfuscate = 0;
    /* Check for --obfuscate flag */
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--obfuscate")) do_obfuscate=1;
    }

    /* Initialize name mangler */
    mangle_init(do_obfuscate);

    if(argc<3){
        fprintf(stderr,
            "shell2c — Shell-to-C Transpiler (modular + obfuscation)\n\n"
            "Author: 爱摸鱼的狐狸 🦊\n\n"
            "Usage: %s input.sh output.c [--makefile] [--run] [--obfuscate]\n\n"
            "Options:\n"
            "  --makefile   Also emit a Makefile for the output\n"
            "  --run        Compile and run the output immediately\n"
            "  --obfuscate  Generate obfuscated C code (anti-analysis)\n",
            argv[0]);
        return 1;
    }
    FILE *fin=fopen(argv[1],"r");
    if(!fin){perror(argv[1]);return 1;}
    Node *script=parse_script(fin);
    fclose(fin);
    
    /* Pre-scan AST to register variables from $((var=...)) in expressions */
    prescan_register_vars(script);

    FILE *fout=fopen(argv[2],"w");
    if(!fout){perror(argv[2]);return 1;}

    fprintf(fout,"/* ============================================================\n");
    fprintf(fout," * Generated by shell2c — Shell-to-C Transpiler\n");
    fprintf(fout," * Author: 爱摸鱼的狐狸\n");
    fprintf(fout," * Source: %s\n",argv[1]);
    if(do_obfuscate)
        fprintf(fout," * Mode: obfuscated (anti-analysis enabled)\n");
    fprintf(fout," * This file is auto-generated. Do not edit manually.\n");
    fprintf(fout," * ============================================================ */\n\n");
    /* Emit runtime header — mangle names if obfuscation is on */
    if(do_obfuscate){
        /* Mangle the entire RT_HEADER string */
        const char *mangled_header = mangle_string(RT_HEADER);
        fprintf(fout, "%s", mangled_header);
    } else {
        fprintf(fout, "%s", RT_HEADER);
    }

    /* Emit obfuscation runtime if requested */
    if(do_obfuscate){
        emit_obfuscation_runtime(fout);
    }

    /* global variables */
    fprintf(fout,"/* ---- user variables ---- */\n");
    for(int i=0;i<=9;i++) fprintf(fout,"static char __sh_arg%d[1024]=\"\";\n",i);
    for(int i=0;i<var_count;i++){
        const char *cn=safe_cname(var_table[i].name);
        if(var_table[i].kind==V_INT)
            fprintf(fout,"static int %s=0;\n",cn);
        else if(var_table[i].kind==V_ARRAY)
            fprintf(fout,"static const char *__arr_%s[64]={NULL};\n",cn);
        else
            fprintf(fout,"static char %s[1024]=\"\";\n",cn);
    }
    fprintf(fout,"\n");

    /* forward declarations */
    fprintf(fout,"/* ---- function declarations ---- */\n");
    { Node *n=script;
      while(n){ if(n->type==NODE_FUNC)
          fprintf(fout,"static void %s(int,char**);\n",safe_cname(n->fname));
        n=n->next; }
    }
    fprintf(fout,"\n");

    /* function definitions */
    fprintf(fout,"/* ---- function definitions ---- */\n");
    emit_functions(fout,script);

    /* main entry point */
    fprintf(fout,"\n/* ---- main entry point ---- */\n");
    fprintf(fout,"int main(int _argc, char **_argv){\n");
    fprintf(fout,"    setvbuf(stdout, NULL, _IONBF, 0);\n");
    fprintf(fout,"    setvbuf(stdin, NULL, _IONBF, 0);\n");
    fprintf(fout,"    __sh_argc = _argc - 1;\n");
    for(int i=0;i<=9;i++)
        fprintf(fout,"    if (_argc > %d) strncpy(__sh_arg%d, _argv[%d], 1023);\n",i,i,i);
    fprintf(fout,"\n");
    emit_node(fout,script);
    /* Inject dead code decoys if obfuscation is enabled */
    if(do_obfuscate){
        emit_decoy_block(fout, 42);
        emit_decoy_block(fout, 137);
        emit_decoy_block(fout, 256);
    }
    fprintf(fout,"    return __exit_status;\n}\n");

    /* Post-process: if obfuscation is on, mangle all names in the output file */
    if(do_obfuscate){
        fclose(fout);
        /* Re-read, mangle, and re-write */
        FILE *fin2 = fopen(argv[2], "r");
        if(fin2){
            fseek(fin2, 0, SEEK_END);
            long fsize = ftell(fin2);
            fseek(fin2, 0, SEEK_SET);
            char *content = malloc(fsize + 1);
            fread(content, 1, fsize, fin2);
            content[fsize] = 0;
            fclose(fin2);

            const char *mangled = mangle_string(content);
            fout = fopen(argv[2], "w");
            if(fout){
                fprintf(fout, "%s", mangled);
                fclose(fout);
            }
            free(content);
        }
    } else {
        fclose(fout);
    }
    printf("[OK] %s -> %s\n",argv[1],argv[2]);

    int make_run=0;
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--makefile")){
            char exe[256]; strncpy(exe,argv[2],255); exe[255]=0;
            char *dot=strrchr(exe,'.'); if(dot)*dot=0;
            FILE *mk=fopen("Makefile","w");
            if(mk){
                fprintf(mk,"CC=gcc\nCFLAGS=-O2 -Wall\nall: %s\n%s: %s\n\t$(CC) $(CFLAGS) -o $@ $<\nclean:\n\trm -f %s\n",
                        exe,exe,argv[2],exe);
                fclose(mk);
                printf("[OK] Makefile written\n");
            }
        }
        if(!strcmp(argv[i],"--run")) make_run=1;
    }
    if(make_run){
        char exe[256]; strncpy(exe,argv[2],255); exe[255]=0;
        char *dot=strrchr(exe,'.'); if(dot)*dot=0;
        char cmd[1024];
        snprintf(cmd,sizeof(cmd),"gcc -O2 -Wall -o %s %s 2>&1",exe,argv[2]);
        printf("[RUN] %s\n",cmd);
        int rc=system(cmd);
        if(rc==0){
            snprintf(cmd,sizeof(cmd),"./%s",exe);
            printf("[RUN] %s\n",cmd);
            system(cmd);
        }
    }
    return 0;
}
