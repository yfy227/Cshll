/* ================================================================== */
/* L11 Main                                                           */
/* ================================================================== */

/* Pre-scan: walk AST and register variables from $((var=...)) in expressions */
static void prescan_register_vars(Node *n){
    while(n){
        if(n->type==NODE_CMD && n->argv){
            /* BUG-03 fix: export VAR=val — register variable before emit */
            if(n->argv[0] && !strcmp(n->argv[0],"export")){
                for(int i=1;i<n->argc;i++){
                    const char *s=n->argv[i];
                    if(!s) continue;
                    const char *eq=strchr(s,'=');
                    if(eq){
                        char vn[128]; int vl=(int)(eq-s);
                        if(vl>=(int)sizeof(vn)) vl=(int)sizeof(vn)-1;
                        memcpy(vn,s,vl); vn[vl]=0;
                        if(vn[0] && !is_known_var(vn))
                            add_var(vn,V_STR);
                    }
                }
            }
            /* BUG-11 fix: let x=expr — register variable before emit */
            if(n->argv[0] && !strcmp(n->argv[0],"let")){
                for(int i=1;i<n->argc;i++){
                    const char *s=n->argv[i];
                    if(!s) continue;
                    /* strip surrounding quotes */
                    const char *ss=s; int slen=(int)strlen(ss);
                    if(slen>=2 && ((ss[0]=='"'&&ss[slen-1]=='"')||(ss[0]=='\''&&ss[slen-1]=='\''))){ ss++; slen-=2; }
                    const char *eq=strchr(ss,'=');
                    if(eq && eq[1]!='='){
                        char vn[128]; int vl=(int)(eq-ss);
                        if(vl>=(int)sizeof(vn)) vl=(int)sizeof(vn)-1;
                        memcpy(vn,ss,vl); vn[vl]=0;
                        if(vn[0] && !is_known_var(vn))
                            add_var(vn,V_INT);
                    }
                }
            }
            /* read -ra var — register var as array before emit */
            if(n->argv[0] && !strcmp(n->argv[0],"read")){
                int ra=0;
                for(int i=1;i<n->argc;i++){
                    if(!n->argv[i]) continue;
                    /* Stop at redirect operators */
                    if(!strcmp(n->argv[i],"<<<")||!strcmp(n->argv[i],"<<")||
                       !strcmp(n->argv[i],"<")||!strcmp(n->argv[i],">")||
                       !strcmp(n->argv[i],">>")||!strcmp(n->argv[i],"2>")) break;
                    if(n->argv[i][0]=='-'){
                        for(const char *fc=n->argv[i];*fc;fc++) if(*fc=='a') ra=1;
                        continue;
                    }
                    if(ra){
                        if(!is_known_var(n->argv[i]))
                            add_var(n->argv[i],V_ARRAY);
                        ra=0; /* only first var after -a */
                    }
                }
            }
            /* BUG-16 fix: printf -v var — register variable before emit */
            if(n->argv[0] && !strcmp(n->argv[0],"printf") && n->argc>=3 && !strcmp(n->argv[1],"-v")){
                if(!is_known_var(n->argv[2]))
                    add_var(n->argv[2],V_STR);
            }
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

/* ---- Large-stack transpiler thread ----
 * Deeply nested shell scripts cause emit_node() to recurse hundreds of
 * times. The default 8MB stack is insufficient for 1000+ levels.
 * We run the transpiler in a pthread with a 128MB stack. */

static int g_argc;
static char **g_argv;
static int g_retcode;

static void *transpile_thread(void *arg){
    (void)arg;
    int argc = g_argc;
    char **argv = g_argv;
    int do_obfuscate = 0;
    int do_vm = 0;
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--obfuscate")) do_obfuscate=1;
        if(!strcmp(argv[i],"--vm")) do_vm=1;
    }
    /* VM mode implies obfuscate — full protection stack */
    if(do_vm) do_obfuscate = 1;

    /* Initialize name mangler */
    mangle_init(do_obfuscate);

    if(argc<3){
        fprintf(stderr,
            "shell2c — Shell-to-C Transpiler (modular + obfuscation + VM)\n\n"
            "Author: 爱摸鱼的狐狸 🦊\n\n"
            "Usage: %s input.sh output.c [options]\n\n"
            "Options:\n"
            "  --makefile   Also emit a Makefile for the output\n"
            "  --run        Compile and run the output immediately\n"
            "  --obfuscate  Generate obfuscated C code (anti-analysis)\n"
            "  --vm         Generate VM-protected output (bytecode + hybrid JIT)\n"
            "               Custom stack-based ISA with hot-path native JIT.\n"
            "               Implies --obfuscate for bytecode encoding.\n",
            argv[0]);
        g_retcode=1; return NULL;
    }
    FILE *fin=fopen(argv[1],"r");
    if(!fin){perror(argv[1]);g_retcode=1;return NULL;}
    Node *script=parse_script(fin);
    fclose(fin);
    
    /* Pre-scan AST to register variables from $((var=...)) in expressions */
    prescan_register_vars(script);

    FILE *fout=fopen(argv[2],"w");
    if(!fout){perror(argv[2]);g_retcode=1;return NULL;}

    /* ---- VM mode: compile to bytecode, emit VM runtime ---- */
    if(do_vm){
        fprintf(fout,"/* ============================================================\n");
        fprintf(fout," * Generated by shell2c — VM-Protected Mode\n");
        fprintf(fout," * Custom stack-based ISA with hot-path JIT\n");
        fprintf(fout," * Author: 爱摸鱼的狐狸 🦊\n");
        fprintf(fout," * Source: %s\n",argv[1]);
        fprintf(fout," * ============================================================ */\n\n");

        /* Emit VM runtime + compile AST to bytecode */
        extern void vmc_compile_and_emit(FILE *out, Node *script_ast, int obfuscate);
        vmc_compile_and_emit(fout, script, 1 /* obfuscate always on for VM */);

        /* Emit obfuscation protections AFTER VM runtime (needs includes) */
        /* Salsa20 cipher (from shconf12 sample) */
        extern const char _SALSA20_HELPERS[];
        fprintf(fout, "%s", _SALSA20_HELPERS);
        /* Anti-IDA noise functions (from IdaTrap sample) */
        extern void emit_noise_functions(FILE*, int);
        emit_noise_functions(fout, 64);
        /* Runtime integrity check (from yiyan sample) */
        extern void emit_integrity_check(FILE*);
        emit_integrity_check(fout);

        fclose(fout);

        /* VM mode always applies name mangling to strip symbols */
        {
            extern const char *mangle_string(const char *s);
            extern int g_obfuscate;
            g_obfuscate = 1; /* enable mangler */
            FILE *fin2 = fopen(argv[2], "r");
            if(fin2){
                fseek(fin2, 0, SEEK_END);
                long fsize = ftell(fin2);
                fseek(fin2, 0, SEEK_SET);
                char *content = malloc(fsize + 1);
                if(content){
                    size_t rd = fread(content, 1, fsize, fin2);
                    content[rd] = 0;
                    fclose(fin2);
                    const char *mangled = mangle_string(content);
                    fout = fopen(argv[2], "w");
                    if(fout){
                        fprintf(fout, "%s", mangled);
                        fclose(fout);
                    }
                    if(mangled != content) free((void*)mangled);
                    free(content);
                } else {
                    fclose(fin2);
                }
            }
        }

        printf("[OK] %s -> %s (VM mode)\n",argv[1],argv[2]);

        /* Handle --makefile and --run */
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
            if(!strcmp(argv[i],"--run")){
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
        }
        return 0;
    }

    fprintf(fout,"/* ============================================================\n");
    fprintf(fout," * Generated by shell2c — Shell-to-C Transpiler\n");
    fprintf(fout," * Author: 爱摸鱼的狐狸\n");
    fprintf(fout," * Source: %s\n",argv[1]);
    if(do_obfuscate)
        fprintf(fout," * Mode: obfuscated (anti-analysis enabled)\n");
    fprintf(fout," * This file is auto-generated. Do not edit manually.\n");
    fprintf(fout," * ============================================================ */\n\n");
    /* Emit runtime header — names will be mangled in post-processing pass */
    fprintf(fout, "%s", RT_HEADER);

    /* Emit obfuscation runtime if requested */
    if(do_obfuscate){
        emit_obfuscation_runtime(fout);
        /* Salsa20 cipher (from shconf12 sample) */
        extern const char _SALSA20_HELPERS[];
        fprintf(fout, "%s", _SALSA20_HELPERS);
        /* Anti-IDA noise functions (from IdaTrap sample) */
        extern void emit_noise_functions(FILE*, int);
        emit_noise_functions(fout, 64);
        /* Runtime integrity check (from yiyan sample) */
        extern void emit_integrity_check(FILE*);
        emit_integrity_check(fout);
        /* Global variable wiper placeholder — actual wipe in main before return */
        fprintf(fout, "\nstatic void __sh_wipe_globals(void){}\n");
    }

    /* global variables */
    fprintf(fout,"/* ---- user variables ---- */\n");
    /* BUG-18 fix: increase buffer from 1024 to 4096 */
    for(int i=0;i<=9;i++) fprintf(fout,"static char __sh_arg%d[65536]=\"\";\n",i);
    for(int i=0;i<var_count;i++){
        const char *cn=safe_cname(var_table[i].name);
        if(var_table[i].kind==V_INT)
            fprintf(fout,"static int %s=0;\n",cn);
        else if(var_table[i].kind==V_ARRAY)
            fprintf(fout,"static const char *__arr_%s[256]={NULL};\n",cn);
        else
            fprintf(fout,"static char %s[65536]=\"\";\n",cn);
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
    fprintf(fout,"    setvbuf(stderr, NULL, _IONBF, 0);\n");
    fprintf(fout,"    __sh_start_time=time(NULL);\n");
    fprintf(fout,"    __sh_argc = _argc - 1;\n");
    fprintf(fout,"    __sh_args = _argv + 1;\n");
    for(int i=0;i<=9;i++)
        fprintf(fout,"    if (_argc > %d) strncpy(__sh_arg%d, _argv[%d], 4095);\n",i,i,i);
    fprintf(fout,"\n");
    emit_node(fout,script);
    /* Inject dead code decoys if obfuscation is enabled */
    if(do_obfuscate){
        emit_decoy_block(fout, 42);
        emit_decoy_block(fout, 137);
        emit_decoy_block(fout, 256);
    }
    if(do_obfuscate){
        /* Anti-dump: flush and wipe everything before exit */
        fprintf(fout,"    fflush(stdout); fflush(stderr);\n");
        /* Save exit status before wiping */
        fprintf(fout,"    int _saved_rc=__exit_status;\n");
        /* 1. Wipe environ: memset all env strings, then clearenv, then malloc_trim */
        fprintf(fout,"    {extern char **environ; if(environ){char **_ep=environ; while(*_ep){char *_s=*_ep; size_t _l=strlen(_s); memset(_s,0,_l); _ep++;}}}\n");
        fprintf(fout,"    clearenv();\n");
        fprintf(fout,"    malloc_trim(0);\n");
        /* 2. Wipe ALL BSS — volatile to prevent compiler optimization */
        fprintf(fout,"    {extern volatile char __bss_start[]; extern volatile char _end[]; "
                     "size_t _bss_size=(size_t)((char*)_end-(char*)__bss_start); "
                     "volatile char *_p=(volatile char*)__bss_start; "
                     "while(_p<(volatile char*)_end){*_p=0; _p++;}}\n");
        /* 3. _exit immediately — no malloc_trim to avoid glibc reentry */
        /* 4. _exit — kernel reclaims memory */
        fprintf(fout,"    _exit(_saved_rc);\n");
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
            if(content){
                size_t rd = fread(content, 1, fsize, fin2);
                content[rd] = 0;
                fclose(fin2);

                const char *mangled = mangle_string(content);
                fout = fopen(argv[2], "w");
                if(fout){
                    fprintf(fout, "%s", mangled);
                    fclose(fout);
                }
                /* mangle_string returns malloc'd buffer if it modified anything,
                 * or the original pointer if unchanged. Safe to free either way
                 * since 'content' is freed next. */
                if(mangled != content) free((void*)mangled);
                free(content);
            } else {
                fclose(fin2);
            }
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
    g_retcode = 0;
    return NULL;
}


/* ============================================================
 * -all mode: Interactive terminal interpreter mode
 * shell2c becomes a complete shell — uses built-in runtime
 * functions directly, not system() fallback
 * ============================================================ */

/* Include the live runtime (same code as emitted into output C files) */
#include "src/s2c_runtime_live.h"

static void init_terminal_env(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    setenv("SHELL","/bin/sh",1);
    setenv("TERM","xterm-256color",1);
    setenv("LANG","C.UTF-8",1);
    setenv("LC_ALL","C.UTF-8",1);
    setenv("PS1","\\u@\\h:\\w$ ",1);
    const char *path=getenv("PATH");
    if(!path || !*path){
        setenv("PATH","/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/snap/bin",1);
    } else {
        char np[4096]; snprintf(np,sizeof(np),"%s",path);
        const char *extra[]={"/usr/local/sbin","/usr/local/bin","/usr/sbin","/usr/bin","/sbin","/bin","/snap/bin",NULL};
        for(int i=0;extra[i];i++){ if(!strstr(path,extra[i])){ strncat(np,":",sizeof(np)-strlen(np)-1); strncat(np,extra[i],sizeof(np)-strlen(np)-1); } }
        setenv("PATH",np,1);
    }
    if(!getenv("HOME")||!*getenv("HOME")){ char hm[256]; snprintf(hm,sizeof(hm),"/home/%s",getenv("USER")?getenv("USER"):"root"); setenv("HOME",hm,1); }
    setenv("IFS"," \t\n",1);
}

static char *expand_vars(const char *line){
    static char buf[16384]; int bi=0;
    const char *s=line;
    while(*s && bi<(int)sizeof(buf)-4){
        if(*s=='$' && s[1]){
            char vn[128]; int vi=0; s++;
            if(*s=='{'){ s++; while(*s&&*s!='}'&&vi<127) vn[vi++]=*s++; if(*s)s++; }
            else if(*s=='('){ s++; int d=1; char cmd[4096]; int ci=0;
                while(*s&&d&&ci<4095){ if(*s=='(')d++; else if(*s==')'){d--;if(d==0){s++;break;}} cmd[ci++]=*s++; }
                cmd[ci]=0;
                FILE *p=popen(cmd,"r"); if(p){ char tb[8192]; size_t n=fread(tb,1,8191,p); tb[n>0?n:0]=0; pclose(p);
                    while(n>0&&tb[n-1]=='\n') tb[--n]=0;
                    for(size_t j=0;j<n&&bi<(int)sizeof(buf)-4;j++) buf[bi++]=tb[j]; }
                continue; }
            else { while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127) vn[vi++]=*s++; }
            vn[vi]=0;
            if(vi>0){ const char *val=getenv(vn); if(val){ int vl=strlen(val); for(int j=0;j<vl&&bi<(int)sizeof(buf)-4;j++) buf[bi++]=val[j]; } }
            else buf[bi++]='$';
            continue;
        }
        if(*s=='~' && (s==line || s[-1]==' ' || s[-1]=='\t')){
            const char *home=getenv("HOME"); if(home){ int hl=strlen(home); for(int j=0;j<hl&&bi<(int)sizeof(buf)-4;j++) buf[bi++]=home[j]; s++; continue; }
        }
        buf[bi++]=*s++;
    }
    buf[bi]=0; return buf;
}

static int exec_external(int argc, char **argv){
    pid_t pid=fork();
    if(pid==0){ execvp(argv[0],argv); fprintf(stderr,"%s: command not found\n",argv[0]); _exit(127); }
    if(pid>0){ int st; waitpid(pid,&st,0); return WIFEXITED(st)?WEXITSTATUS(st):1; }
    return 1;
}

static int try_builtin(int argc, char **argv){
    if(!strcmp(argv[0],"cd")){ const char *d=argc>1?argv[1]:getenv("HOME"); if(!d)d="/"; if(chdir(d)!=0){perror("cd");return 1;} char cwd[1024]; getcwd(cwd,sizeof(cwd)); setenv("PWD",cwd,1); return 0; }
    if(!strcmp(argv[0],"pwd")){ __b_pwd(); return 0; }
    if(!strcmp(argv[0],"echo")){
        for(int i=1;i<argc;i++){ if(i>1) putchar(' '); const char *s=argv[i]; int sl=strlen(s);
            if(sl>=2&&s[0]=='"'&&s[sl-1]=='"'){ s++; sl-=2; }
            while(*s&&sl-->0){ if(*s=='$'&&s[1]){ char vn[128]; int vi=0; s++;
                if(*s=='{'){s++;while(*s&&*s!='}'&&vi<127)vn[vi++]=*s++;if(*s)s++;}
                else{while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127)vn[vi++]=*s++;}
                vn[vi]=0; const char *v=getenv(vn); if(v)fputs(v,stdout);
            } else { putchar(*s); s++; } } }
        putchar('\n'); return 0;
    }
    if(!strcmp(argv[0],"export")){ if(argc<2){__b_env();return 0;} for(int i=1;i<argc;i++){char *eq=strchr(argv[i],'=');if(eq){*eq=0;setenv(argv[i],eq+1,1);}} return 0; }
    if(!strcmp(argv[0],"unset")){ for(int i=1;i<argc;i++) unsetenv(argv[i]); return 0; }
    if(!strcmp(argv[0],"env")){ __b_env(); return 0; }
    if(!strcmp(argv[0],"set")){ if(argc<2){__b_env();return 0;} for(int i=1;i<argc;i++){char *eq=strchr(argv[i],'=');if(eq){*eq=0;setenv(argv[i],eq+1,1);}} return 0; }
    if(!strcmp(argv[0],"which")||!strcmp(argv[0],"type")){ if(argc>1)__b_which(argv[1]); return 0; }
    if(!strcmp(argv[0],"eval")){ char cmd[8192]=""; for(int i=1;i<argc;i++){if(i>1)strcat(cmd," ");strncat(cmd,argv[i],sizeof(cmd)-strlen(cmd)-1);} return system(cmd); }
    if(!strcmp(argv[0],"exec")){ if(argc>1) execvp(argv[1],argv+1); return 1; }
    if(!strcmp(argv[0],"exit")||!strcmp(argv[0],"quit")) return -999;
    if(!strcmp(argv[0],"true")) return 0;
    if(!strcmp(argv[0],"false")) return 1;
    if(!strcmp(argv[0],"clear")){ fputs("\033[2J\033[H",stdout); return 0; }
    if(!strcmp(argv[0],"reset")){ fputs("\033c",stdout); return 0; }
    if(!strcmp(argv[0],"help")){
        printf("shell2c Interactive Shell v2.0\n\n");
        printf("Shell builtins: cd pwd echo export unset env set which eval exec exit help\n");
        printf("Native tools (C implemented):\n");
        printf("  ls cat grep sed tr cut sort uniq wc head tail tac rev nl\n");
        printf("  cp mv rm mkdir rmdir touch ln chmod stat du df\n");
        printf("  basename dirname realpath readlink mktemp tee xargs\n");
        printf("  seq yes date whoami hostname uname nproc id uptime free\n\n");
        printf("Pipes (|) and redirects (< > >> 2>) are natively implemented.\n");
        printf("Package managers: apt apt-get pkg dpkg snap pip npm cargo gem\n");
        return 0;
    }
    /* Runtime builtins — direct C function calls */
    if(!strcmp(argv[0],"ls")){ __b_ls(argc>1?argv[1]:"",NULL); return __exit_status; }
    if(!strcmp(argv[0],"cat")){ __b_cat(argc>1?argv[1]:NULL,0); return __exit_status; }
    if(!strcmp(argv[0],"grep")){ __b_grep(argc>1?argv[1]:"",argc>2?argv[2]:NULL,argc>3?argv[3]:""); return __exit_status; }
    if(!strcmp(argv[0],"sed")){ __b_sed(argc>1?argv[1]:"",argc>2?argv[2]:NULL); return __exit_status; }
    if(!strcmp(argv[0],"tr")){ if(argc>=3)__b_tr(argv[1],argv[2]); if(argc>1&&!strcmp(argv[1],"-d")&&argc>2)__b_tr_delete(argv[2]); return __exit_status; }
    if(!strcmp(argv[0],"cut")){ __b_cut(argc>1?argv[1]:"",' ',argc>2?argv[2]:NULL); return __exit_status; }
    if(!strcmp(argv[0],"sort")){ __b_sort(argc>1?argv[1]:NULL,0,0,0); return __exit_status; }
    if(!strcmp(argv[0],"uniq")){ __b_uniq(argc>1?argv[1]:NULL,0); return __exit_status; }
    if(!strcmp(argv[0],"wc")){
        int dl=0,dw=0,dc=0; const char *path=NULL;
        for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"-l"))dl=1; else if(!strcmp(argv[i],"-w"))dw=1; else if(!strcmp(argv[i],"-c"))dc=1; else path=argv[i]; }
        if(!dl&&!dw&&!dc){dl=1;dw=1;dc=1;} /* no flags = all */
        __b_wc(path,dl,dw,dc); return __exit_status;
    }
    if(!strcmp(argv[0],"head")){ int n=10; const char *p=NULL; for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"-n")&&i+1<argc)n=atoi(argv[++i]); else if(argv[i][0]=='-'&&isdigit((unsigned char)argv[i][1]))n=atoi(argv[i]+1); else p=argv[i]; } __b_head(p,n); return __exit_status; }
    if(!strcmp(argv[0],"tail")){ int n=10; const char *p=NULL; for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"-n")&&i+1<argc)n=atoi(argv[++i]); else if(argv[i][0]=='-'&&isdigit((unsigned char)argv[i][1]))n=atoi(argv[i]+1); else p=argv[i]; } __b_tail(p,n); return __exit_status; }
    if(!strcmp(argv[0],"tac")){ __b_tac(argc>1?argv[1]:NULL); return __exit_status; }
    if(!strcmp(argv[0],"rev")){ __b_rev(argc>1?argv[1]:NULL); return __exit_status; }
    if(!strcmp(argv[0],"nl")){ __b_nl(argc>1?argv[1]:NULL); return __exit_status; }
    if(!strcmp(argv[0],"seq")){ __b_seq(argc>1?atoi(argv[1]):1,1,argc>2?atoi(argv[2]):1); return __exit_status; }
    if(!strcmp(argv[0],"yes")){ __b_yes(argc>1?argv[1]:NULL); return 0; }
    if(!strcmp(argv[0],"date")){ __b_date(argc>1?argv[1]:NULL); return 0; }
    if(!strcmp(argv[0],"whoami")){ __b_whoami(); return 0; }
    if(!strcmp(argv[0],"hostname")){ if(argc>1)__b_hostname_set(argv[1]); else __b_hostname(); return 0; }
    if(!strcmp(argv[0],"uname")){ __b_uname(argc>1); return 0; }
    if(!strcmp(argv[0],"nproc")){ __b_nproc(); return 0; }
    if(!strcmp(argv[0],"id")){ __b_id(); return 0; }
    if(!strcmp(argv[0],"uptime")){ __b_uptime(); return 0; }
    if(!strcmp(argv[0],"free")){ __b_free_mem(); return 0; }
    if(!strcmp(argv[0],"cp")&&argc>=3){ __b_cp(argv[1],argv[2]); return __exit_status; }
    if(!strcmp(argv[0],"mv")&&argc>=3){ __b_mv(argv[1],argv[2]); return __exit_status; }
    if(!strcmp(argv[0],"rm")){ if(argc>1)unlink(argv[1]); return 0; }
    if(!strcmp(argv[0],"mkdir")&&argc>=2){ mkdir(argv[1],0755); return 0; }
    if(!strcmp(argv[0],"rmdir")&&argc>=2){ rmdir(argv[1]); return 0; }
    if(!strcmp(argv[0],"touch")&&argc>=2){ __b_touch(argv[1]); return 0; }
    if(!strcmp(argv[0],"stat")&&argc>=2){ __b_stat(argv[1]); return 0; }
    if(!strcmp(argv[0],"basename")&&argc>=2){ __b_basename(argv[1]); return 0; }
    if(!strcmp(argv[0],"dirname")&&argc>=2){ __b_dirname(argv[1]); return 0; }
    if(!strcmp(argv[0],"realpath")&&argc>=2){ __b_realpath(argv[1]); return 0; }
    if(!strcmp(argv[0],"tee")){ __b_tee(0,NULL); return 0; }
    return -1;
}

/* Execute a command line with native pipe+redirect support */
static int execute_line(char *line){
    int last_rc=0;
    /* Find pipe | (not ||) */
    char *pipe_pos=NULL;
    for(char *p=line;*p;p++){
        if(*p=='|'&&p[1]!='|'){ if(p>line&&p[-1]=='|')continue; pipe_pos=p; break; }
    }
    if(pipe_pos){
        char *stages[16]; int ns=0;
        stages[ns++]=line;
        char *p=pipe_pos;
        while(p){ *p=0; p++; while(*p==' ')p++; if(*p)stages[ns++]=p; else break;
            p=strchr(p,'|'); while(p&&p[1]=='|')p=strchr(p+2,'|'); }
        if(ns>=2){
            int prev_read=-1; pid_t pids[16];
            for(int i=0;i<ns;i++){
                int pfd[2]; if(i<ns-1)pipe(pfd);
                pid_t pid=fork();
                if(pid==0){
                    if(prev_read>=0){dup2(prev_read,0);close(prev_read);}
                    if(i<ns-1){close(pfd[0]);dup2(pfd[1],1);close(pfd[1]);}
                    char *exp=expand_vars(stages[i]);
                    char *tk[64]; int nt=0; char *t=strtok(exp," \t");
                    while(t&&nt<63){tk[nt++]=t;t=strtok(NULL," \t");} tk[nt]=NULL;
                    if(nt>0){ int rc=try_builtin(nt,tk); if(rc==-999)_exit(0); if(rc<0)exec_external(nt,tk); }
                    fflush(stdout); _exit(0);
                }
                pids[i]=pid;
                if(prev_read>=0)close(prev_read);
                if(i<ns-1){close(pfd[1]);prev_read=pfd[0];}
            }
            if(prev_read>=0)close(prev_read);
            for(int i=0;i<ns;i++)waitpid(pids[i],NULL,0);
            return 0;
        }
    }
    /* && and || */
    char *and=strstr(line,"&&"),*or=strstr(line,"||");
    if(and||or){
        char *op=(and&&(!or||and<or))?and:or; int is_and=(op==and);
        *op=0; char *rest=op+2; while(*rest==' ')rest++;
        int rc=execute_line(line);
        if(is_and){ if(rc==0)return execute_line(rest); return rc; }
        else { if(rc!=0)return execute_line(rest); return 0; }
    }
    /* Redirections */
    int sin=-1,sout=-1,serr=-1;
    /* 2>&1 */
    char *m=strstr(line,"2>&1"); if(m){ *m=0; serr=dup(2); dup2(1,2); }
    /* >> file */
    char *gt2=strstr(line," >> ");
    if(gt2){ *gt2=0; char *f=gt2+4; char *sp=strchr(f,' '); if(sp)*sp=0;
        int fd=open(f,O_WRONLY|O_CREAT|O_APPEND,0644);
        if(fd>=0){sout=dup(1);dup2(fd,1);close(fd);}else perror(f);
    } else {
        char *gt=strstr(line," > "); if(!gt){gt=strstr(line,">");if(gt&&(gt==line||gt[-1]==' '||gt[-1]=='2')){}else gt=NULL;}
        if(gt){ *gt=0; gt++; if(*gt=='>')gt++; while(*gt==' ')gt++; char *f=gt; char *sp=strchr(f,' '); if(sp)*sp=0;
            int fd=open(f,O_WRONLY|O_CREAT|O_TRUNC,0644);
            if(fd>=0){sout=dup(1);dup2(fd,1);close(fd);}else perror(f);
        }
    }
    /* < file */
    char *lt=strstr(line," < "); if(!lt){lt=strstr(line,"<");if(lt&&(lt==line||lt[-1]==' ')){}else lt=NULL;}
    if(lt){ *lt=0; lt++; while(*lt==' ')lt++; char *f=lt; char *sp=strchr(f,' '); if(sp)*sp=0;
        int fd=open(f,O_RDONLY);
        if(fd>=0){sin=dup(0);dup2(fd,0);close(fd);}else perror(f);
    }
    /* 2> file */
    char *e2=strstr(line," 2> "); if(!e2){e2=strstr(line,"2>");if(e2&&(e2==line||e2[-1]==' ')){}else e2=NULL;}
    if(e2){ *e2=0; e2+=2; while(*e2==' ')e2++; char *f=e2; char *sp=strchr(f,' '); if(sp)*sp=0;
        int fd=open(f,O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd>=0){if(serr<0)serr=dup(2);dup2(fd,2);close(fd);}else perror(f);
    }
    /* Execute command */
    char *exp=expand_vars(line);
    char *tk[64]; int nt=0; char *t=strtok(exp," \t");
    while(t&&nt<63){tk[nt++]=t;t=strtok(NULL," \t");} tk[nt]=NULL;
    if(nt>0){ int rc=try_builtin(nt,tk); if(rc==-999)last_rc=-999; else if(rc>=0)last_rc=rc; else last_rc=exec_external(nt,tk); }
    /* Restore */
    fflush(stdout);fflush(stderr);
    if(sin>=0){dup2(sin,0);close(sin);}
    if(sout>=0){dup2(sout,1);close(sout);}
    if(serr>=0){dup2(serr,2);close(serr);}
    return last_rc;
}

/* Generate a self-bootstrapping terminal C file.
 * The generated terminal can:
 *   1. Auto-install gcc + git (apt/pkg)
 *   2. Clone the project repo
 *   3. Compile the project
 *   4. Use the compiled shell2c inside the terminal
 */
int shell2c_all_mode(int argc, char **argv){
    /* Parameters */
    const char *repo_url = "https://github.com/yfy227/Cshll.git";
    const char *output_file = "terminal.c";
    const char *project_name = "Cshll";

    for(int i=1;i<argc;i++){
        if((!strcmp(argv[i],"-o")||!strcmp(argv[i],"--output")) && i+1<argc)
            output_file = argv[++i];
        if((!strcmp(argv[i],"-r")||!strcmp(argv[i],"--repo")) && i+1<argc)
            repo_url = argv[++i];
        if((!strcmp(argv[i],"-n")||!strcmp(argv[i],"--name")) && i+1<argc)
            project_name = argv[++i];
    }

    FILE *f = fopen(output_file, "w");
    if(!f){ perror(output_file); return 1; }

    /* Write a fork+execvp based command executor that replaces system().
     * This makes the terminal NOT depend on external bash/sh.
     * All system() calls in runtime functions are redirected via #define. */
    fprintf(f,
"/* ---- native exec: replaces system() with fork+execvp ---- */\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <string.h>\n"
"#include <ctype.h>\n"
"#include <unistd.h>\n"
"#include <fcntl.h>\n"
"#include <sys/wait.h>\n"
"#include <sys/types.h>\n"
"\n"
"/* Brace expansion: {1..5} → 1 2 3 4 5, {a,b,c} → a b c */\n"
"static int __sh_expand_braces(const char *src, char *dst, int dstsz){\n"
"  int di=0; const char *s=src;\n"
"  while(*s && di<dstsz-2){\n"
"    if(*s=='{'){\n"
"      const char *end=strchr(s,'}');\n"
"      if(end && end-s>2){\n"
"        char inner[256]; int il=end-s-1;\n"
"        if(il<256){\n"
"          memcpy(inner,s+1,il); inner[il]=0;\n"
"          /* Range: {1..10} or {a..z} */\n"
"          char *dot=strstr(inner,\"..\");\n"
"          if(dot){\n"
"            *dot=0; char *start=inner; char *endp=dot+2;\n"
"            if(isdigit((unsigned char)start[0])&&isdigit((unsigned char)endp[0])){\n"
"              int a=atoi(start), b=atoi(endp);\n"
"              if(a<=b&&b-a<100000){\n"
"                for(int n=a;n<=b&&di<dstsz-4;n++){\n"
"                  if(n>a){ dst[di++]=' '; }\n"
"                  di+=snprintf(dst+di,dstsz-di,\"%d\",n);\n"
"                }\n"
"                s=end+1; continue;\n"
"              }\n"
"            } else if(isalpha((unsigned char)start[0])&&isalpha((unsigned char)endp[0])&&start[1]==0&&endp[1]==0){\n"
"              char a=start[0], b=endp[0];\n"
"              if(a<=b){\n"
"                for(char c=a;c<=b&&di<dstsz-4;c++){\n"
"                  if(c>a){ dst[di++]=' '; }\n"
"                  dst[di++]=c;\n"
"                }\n"
"                s=end+1; continue;\n"
"              }\n"
"            }\n"
"          } else {\n"
"            /* List: {a,b,c} */\n"
"            char *tok=strtok(inner,\",\");\n"
"            while(tok){\n"
"              if(di>0 && dst[di-1]!=' '){ dst[di++]=' '; }\n"
"              int tl=strlen(tok);\n"
"              if(di+tl<dstsz-2){ memcpy(dst+di,tok,tl); di+=tl; }\n"
"              tok=strtok(NULL,\",\");\n"
"            }\n"
"            s=end+1; continue;\n"
"          }\n"
"        }\n"
"      }\n"
"    }\n"
"    dst[di++]=*s++;\n"
"  }\n"
"  dst[di]=0; return di;\n"
"}\n"
"\n"
"static int __sh_exec_simple(const char *cmd){\n"
"  /* Expand braces first ({1..5} → 1 2 3 4 5) */\n"
"  char expanded[65536];\n"
"  if(strchr(cmd,'{')) __sh_expand_braces(cmd,expanded,sizeof(expanded));\n"
"  else { strncpy(expanded,cmd,sizeof(expanded)-1); expanded[sizeof(expanded)-1]=0; }\n"
"  /* Parse command into argv, fork+execvp (no sh needed) */\n"
"  char buf[65536]; strncpy(buf,expanded,sizeof(buf)-1); buf[sizeof(buf)-1]=0;\n"
"  char *argv[4096]; int argc=0;\n"
"  char *p=buf;\n"
"  while(*p && argc<4095){\n"
"    while(*p==' '||*p=='\\t') p++;\n"
"    if(!*p) break;\n"
"    argv[argc++]=p;\n"
"    if(*p=='\"'){ p++; while(*p&&*p!='\"'){p++;} if(*p)p++; }\n"
"    else if(*p=='\\''){ p++; while(*p&&*p!='\\''){p++;} if(*p)p++; }\n"
"    else { while(*p&&*p!=' '&&*p!='\\t') p++; }\n"
"    if(*p){ *p=0; p++; }\n"
"  }\n"
"  argv[argc]=NULL;\n"
"  if(argc==0) return 0;\n"
"  /* Strip quotes from each arg */\n"
"  for(int i=0;i<argc;i++){\n"
"    int sl=strlen(argv[i]);\n"
"    if(sl>=2 && ((argv[i][0]=='\"'&&argv[i][sl-1]=='\"')||(argv[i][0]=='\\''&&argv[i][sl-1]=='\\''))){\n"
"      argv[i]++; argv[i][sl-2]=0;\n"
"    }\n"
"  }\n"
"  pid_t pid=fork();\n"
"  if(pid==0){\n"
"    execvp(argv[0],argv);\n"
"    fprintf(stderr,\"%%s: command not found\\n\",argv[0]);\n"
"    _exit(127);\n"
"  }\n"
"  if(pid>0){ int st; waitpid(pid,&st,0); return WIFEXITED(st)?WEXITSTATUS(st):1; }\n"
"  return 1;\n"
"}\n"
"\n"
"static int __sh_exec_cmd(const char *cmd){\n"
"  if(!cmd||!*cmd) return 0;\n"
"  /* All commands go through __sh_exec_simple (fork+execvp, no sh) */\n"
"  return __sh_exec_simple(cmd);\n"
"}\n"
"\n"
"/* Redirect ALL system() calls to fork+execvp implementation */\n"
"#define system __sh_exec_cmd\n"
"\n"
);

    /* Write popen/pclose replacements (fork+pipe+execvp, no sh) */
    fprintf(f,
"/* ---- popen replacement: fork+pipe+execvp, no sh ---- */\n"
"static FILE *__sh_popen_native(const char *cmd, const char *mode){\n"
"  int pfd[2]; if(pipe(pfd)<0) return NULL;\n"
"  pid_t pid=fork();\n"
"  if(pid<0){ close(pfd[0]); close(pfd[1]); return NULL; }\n"
"  if(pid==0){\n"
"    close(pfd[0]); dup2(pfd[1],1); close(pfd[1]);\n"
"    __sh_exec_simple(cmd);\n"
"    _exit(127);\n"
"  }\n"
"  close(pfd[1]);\n"
"  return fdopen(pfd[0],mode);\n"
"}\n"
"static int __sh_pclose_native(FILE *p){\n"
"  int st; fclose(p); wait(&st); return WIFEXITED(st)?WEXITSTATUS(st):-1;\n"
"}\n"
"#define popen __sh_popen_native\n"
"#define pclose __sh_pclose_native\n"
"\n"
);

    /* Write the full runtime library — system() and popen() both replaced */
    fputs(RT_HEADER, f);

    /* Write the terminal bootstrap + interactive shell code */

    /* Write terminal bootstrap + interactive shell code (uses built-in runtime) */
    fprintf(f,
"\n"
"static const char *REPO_URL = \"%s\";\n"
"static const char *PROJECT = \"%s\";\n"
"\n"
"/* run(): execute via __sh_exec_cmd (fork+execvp, no sh) */\n"
"static int run(const char *cmd){printf(\"  $ %%s\\n\",cmd);fflush(stdout);return __sh_exec_cmd(cmd);}\n"
"\n"
"/* tool_exists(): search PATH directly, no system() */\n"
"static int tool_exists(const char *name){\n"
"  const char *path=getenv(\"PATH\"); if(!path) path=\"/usr/bin:/bin\";\n"
"  char pc[4096]; strncpy(pc,path,sizeof(pc)-1); pc[sizeof(pc)-1]=0;\n"
"  char *tk=strtok(pc,\":\");\n"
"  while(tk){char fp[1024];snprintf(fp,sizeof(fp),\"%%s/%%s\",tk,name);if(access(fp,X_OK)==0)return 1;tk=strtok(NULL,\":\");}\n"
"  return 0;\n"
"}\n"
"\n"
"/* pkg_install(): native fork+execvp, no system()/sh */\n"
"static int pkg_install(const char *pkgs){\n"
"  printf(\"[*] Installing: %%s\\n\",pkgs);\n"
"  char pb[512];strncpy(pb,pkgs,sizeof(pb)-1);pb[sizeof(pb)-1]=0;\n"
"  char *pv[32];int pc=0;char *t=strtok(pb,\" \");\n"
"  while(t&&pc<31){pv[pc++]=t;t=strtok(NULL,\" \");}pv[pc]=NULL;\n"
"  const char*mgrs[]={\"apt-get\",\"apk\",\"dnf\",\"yum\",\"pkg\",\"brew\"};\n"
"  const char*inst[]={{\"update\",\"-qq\",NULL},{\"add\",\"--no-cache\",NULL},{\"install\",\"-y\",NULL},{\"install\",\"-y\",NULL},{\"install\",\"-y\",NULL},{\"install\",NULL,NULL}};\n"
"  for(int i=0;i<6;i++){\n"
"    if(!tool_exists(mgrs[i]))continue;\n"
"    if(i==0){char*u[]={\"apt-get\",\"update\",\"-qq\",NULL};pid_t p=fork();if(p==0){execvp(u[0],u);_exit(127);}if(p>0){int st;waitpid(p,&st,0);}}\n"
"    char*av[40];int ai=0;av[ai++]=(char*)mgrs[i];\n"
"    for(int j=0;inst[i][j];j++)av[ai++]=(char*)inst[i][j];\n"
"    for(int j=0;j<pc;j++)av[ai++]=pv[j];av[ai]=NULL;\n"
"    printf(\"  $ %%s\",mgrs[i]);for(int j=1;j<ai;j++)printf(\" %%s\",av[j]);printf(\"\\n\");fflush(stdout);\n"
"    pid_t pid=fork();if(pid==0){execvp(av[0],av);fprintf(stderr,\"%%s: not found\\n\",av[0]);_exit(127);}\n"
"    if(pid>0){int st;waitpid(pid,&st,0);return WIFEXITED(st)?WEXITSTATUS(st):1;}return 1;\n"
"  }\n"
"  fprintf(stderr,\"[!] No package manager. Install manually: %%s\\n\",pkgs);return 1;\n"
"}\n"
"\n"
"/* exec_external: fork+execvp for external commands (defined before try_builtin) */\n"
"static int exec_external(int argc,char **argv){pid_t pid=fork();if(pid==0){execvp(argv[0],argv);fprintf(stderr,\"%%s: not found\\n\",argv[0]);_exit(127);}if(pid>0){int st;waitpid(pid,&st,0);return WIFEXITED(st)?WEXITSTATUS(st):1;}return 1;}\n"
"\n"
"static int try_builtin(int argc,char **argv){\n"
"  if(!strcmp(argv[0],\"cd\")){const char*d=argc>1?argv[1]:getenv(\"HOME\");if(!d)d=\"/\";if(chdir(d)!=0){perror(\"cd\");return 1;}char c[1024];getcwd(c,sizeof(c));setenv(\"PWD\",c,1);return 0;}\n"
"  if(!strcmp(argv[0],\"pwd\")){__b_pwd();return 0;}\n"
"  if(!strcmp(argv[0],\"echo\")){for(int i=1;i<argc;i++){if(i>1)putchar(' ');const char*s=argv[i];int sl=strlen(s);if(sl>=2&&s[0]=='\"'&&s[sl-1]=='\"'){s++;sl-=2;}while(*s&&sl-->0){if(*s=='$'&&s[1]){char vn[128];int vi=0;s++;if(*s=='{'){s++;while(*s&&*s!='}'&&vi<127)vn[vi++]=*s++;if(*s)s++;}else{while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127)vn[vi++]=*s++;}vn[vi]=0;const char*v=getenv(vn);if(v)fputs(v,stdout);}else{putchar(*s);s++;}}}putchar('\\n');return 0;}\n"
"  if(!strcmp(argv[0],\"export\")){if(argc<2){__b_env();return 0;}for(int i=1;i<argc;i++){char*eq=strchr(argv[i],'=');if(eq){*eq=0;setenv(argv[i],eq+1,1);}}return 0;}\n"
"  if(!strcmp(argv[0],\"unset\")){for(int i=1;i<argc;i++)unsetenv(argv[i]);return 0;}\n"
"  if(!strcmp(argv[0],\"env\")){__b_env();return 0;}\n"
"  if(!strcmp(argv[0],\"which\")||!strcmp(argv[0],\"type\")){if(argc>1)__b_which(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"exit\")||!strcmp(argv[0],\"quit\"))return -999;\n"
"  if(!strcmp(argv[0],\"true\"))return 0;\n"
"  if(!strcmp(argv[0],\"false\"))return 1;\n"
"  if(!strcmp(argv[0],\"clear\")){fputs(\"\\033[2J\\033[H\",stdout);return 0;}\n"
"  if(!strcmp(argv[0],\"reset\")){fputs(\"\\033c\",stdout);return 0;}\n"
"  if(!strcmp(argv[0],\"help\")){printf(\"Built-in (native C): cd pwd echo export unset env which exit clear\\n  ls cat grep sed tr cut sort uniq wc head tail tac rev nl\\n  cp mv rm mkdir rmdir touch stat basename dirname realpath tee\\n  seq yes date whoami hostname uname nproc id uptime free\\nPipes(|) redirects(< > >> 2>) native. External: git make gcc apt.\\n\");return 0;}\n"
"  if(!strcmp(argv[0],\"ls\")){__b_ls(argc>1?argv[1]:\"\",NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"cat\")){__b_cat(argc>1?argv[1]:NULL,0);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"grep\")){__b_grep(argc>1?argv[1]:\"\",argc>2?argv[2]:NULL,argc>3?argv[3]:\"\");return __exit_status;}\n"
"  if(!strcmp(argv[0],\"sed\")){__b_sed(argc>1?argv[1]:\"\",argc>2?argv[2]:NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"tr\")){if(argc>=3)__b_tr(argv[1],argv[2]);if(argc>1&&!strcmp(argv[1],\"-d\")&&argc>2)__b_tr_delete(argv[2]);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"cut\")){__b_cut(argc>1?argv[1]:\"\",' ',argc>2?argv[2]:NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"sort\")){__b_sort(argc>1?argv[1]:NULL,0,0,0);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"uniq\")){__b_uniq(argc>1?argv[1]:NULL,0);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"wc\")){int dl=0,dw=0,dc=0;const char*p=NULL;for(int i=1;i<argc;i++){if(!strcmp(argv[i],\"-l\"))dl=1;else if(!strcmp(argv[i],\"-w\"))dw=1;else if(!strcmp(argv[i],\"-c\"))dc=1;else p=argv[i];}if(!dl&&!dw&&!dc){dl=1;dw=1;dc=1;}__b_wc(p,dl,dw,dc);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"head\")){int n=10;const char*p=NULL;for(int i=1;i<argc;i++){if(!strcmp(argv[i],\"-n\")&&i+1<argc)n=atoi(argv[++i]);else if(argv[i][0]=='-'&&isdigit((unsigned char)argv[i][1]))n=atoi(argv[i]+1);else p=argv[i];}__b_head(p,n);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"tail\")){int n=10;const char*p=NULL;for(int i=1;i<argc;i++){if(!strcmp(argv[i],\"-n\")&&i+1<argc)n=atoi(argv[++i]);else if(argv[i][0]=='-'&&isdigit((unsigned char)argv[i][1]))n=atoi(argv[i]+1);else p=argv[i];}__b_tail(p,n);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"tac\")){__b_tac(argc>1?argv[1]:NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"rev\")){__b_rev(argc>1?argv[1]:NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"nl\")){__b_nl(argc>1?argv[1]:NULL);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"seq\")){__b_seq(argc>1?atoi(argv[1]):1,1,argc>2?atoi(argv[2]):1);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"yes\")){__b_yes(argc>1?argv[1]:NULL);return 0;}\n"
"  if(!strcmp(argv[0],\"date\")){__b_date(argc>1?argv[1]:NULL);return 0;}\n"
"  if(!strcmp(argv[0],\"whoami\")){__b_whoami();return 0;}\n"
"  if(!strcmp(argv[0],\"hostname\")){if(argc>1)__b_hostname_set(argv[1]);else __b_hostname();return 0;}\n"
"  if(!strcmp(argv[0],\"uname\")){__b_uname(argc>1);return 0;}\n"
"  if(!strcmp(argv[0],\"nproc\")){__b_nproc();return 0;}\n"
"  if(!strcmp(argv[0],\"id\")){__b_id();return 0;}\n"
"  if(!strcmp(argv[0],\"uptime\")){__b_uptime();return 0;}\n"
"  if(!strcmp(argv[0],\"free\")){__b_free_mem();return 0;}\n"
"  if(!strcmp(argv[0],\"cp\")&&argc>=3){__b_cp(argv[1],argv[2]);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"mv\")&&argc>=3){__b_mv(argv[1],argv[2]);return __exit_status;}\n"
"  if(!strcmp(argv[0],\"rm\")){if(argc>1)unlink(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"mkdir\")&&argc>=2){mkdir(argv[1],0755);return 0;}\n"
"  if(!strcmp(argv[0],\"rmdir\")&&argc>=2){rmdir(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"touch\")&&argc>=2){__b_touch(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"stat\")&&argc>=2){__b_stat(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"basename\")&&argc>=2){__b_basename(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"dirname\")&&argc>=2){__b_dirname(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"realpath\")&&argc>=2){__b_realpath(argv[1]);return 0;}\n"
"  if(!strcmp(argv[0],\"tee\")){__b_tee(0,NULL);return 0;}\n"
"  /* Package management tools — direct fork+execvp, no system()/sh */\n"
"  /* exec_external defined above for use here */\n"
"  if(!strcmp(argv[0],\"apt\")||!strcmp(argv[0],\"apt-get\")||!strcmp(argv[0],\"apt-cache\")||\n"
"     !strcmp(argv[0],\"dpkg\")||!strcmp(argv[0],\"snap\")||!strcmp(argv[0],\"pkg\")||\n"
"     !strcmp(argv[0],\"pip\")||!strcmp(argv[0],\"pip3\")||!strcmp(argv[0],\"npm\")||\n"
"     !strcmp(argv[0],\"cargo\")||!strcmp(argv[0],\"gem\")||!strcmp(argv[0],\"yarn\")||\n"
"     !strcmp(argv[0],\"brew\")||!strcmp(argv[0],\"conda\")||!strcmp(argv[0],\"nix\")||\n"
"     !strcmp(argv[0],\"go\")||!strcmp(argv[0],\"rustc\")||!strcmp(argv[0],\"cmake\")||\n"
"     !strcmp(argv[0],\"gcc\")||!strcmp(argv[0],\"g++\")||!strcmp(argv[0],\"cc\")||\n"
"     !strcmp(argv[0],\"make\")||!strcmp(argv[0],\"git\")||!strcmp(argv[0],\"curl\")||\n"
"     !strcmp(argv[0],\"wget\")||!strcmp(argv[0],\"ssh\")||!strcmp(argv[0],\"scp\")||\n"
"     !strcmp(argv[0],\"tar\")||!strcmp(argv[0],\"zip\")||!strcmp(argv[0],\"unzip\")||\n"
"     !strcmp(argv[0],\"gzip\")||!strcmp(argv[0],\"gunzip\")){\n"
"    return exec_external(argc,argv);\n"
"  }\n"
"  return -1;\n"
"}\n"
"\n"
"static char *expand_vars(const char *line){static char buf[16384];int bi=0;const char*s=line;\n"
"  while(*s&&bi<(int)sizeof(buf)-4){if(*s=='$'&&s[1]){char vn[128];int vi=0;s++;if(*s=='{'){s++;while(*s&&*s!='}'&&vi<127)vn[vi++]=*s++;if(*s)s++;}else if(*s=='('){s++;int d=1;char cmd[4096];int ci=0;while(*s&&d&&ci<4095){if(*s=='(')d++;else if(*s==')'){d--;if(d==0){s++;break;}}cmd[ci++]=*s++;}cmd[ci]=0;FILE*p=popen(cmd,\"r\");if(p){char tb[8192];size_t n=fread(tb,1,8191,p);tb[n>0?n:0]=0;pclose(p);while(n>0&&tb[n-1]=='\\n')tb[--n]=0;for(size_t j=0;j<n&&bi<(int)sizeof(buf)-4;j++)buf[bi++]=tb[j];}continue;}else{while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127)vn[vi++]=*s++;}vn[vi]=0;if(vi>0){const char*val=getenv(vn);if(val){int vl=strlen(val);for(int j=0;j<vl&&bi<(int)sizeof(buf)-4;j++)buf[bi++]=val[j];}}else buf[bi++]='$';continue;}\n"
"  if(*s=='~'&&(s==line||s[-1]==' ')){const char*h=getenv(\"HOME\");if(h){int hl=strlen(h);for(int j=0;j<hl&&bi<(int)sizeof(buf)-4;j++)buf[bi++]=h[j];s++;continue;}}\n"
"  buf[bi++]=*s++;}\n"
"  buf[bi]=0;return buf;}\n"
"\n"
"static int execute_line(char *line){\n"
"  int last_rc=0;\n"
"  char *pp=NULL;for(char*p=line;*p;p++){if(*p=='|'&&p[1]!='|'){if(p>line&&p[-1]=='|')continue;pp=p;break;}}\n"
"  if(pp){char*st[16];int ns=0;st[ns++]=line;char*p=pp;\n"
"    while(p){*p=0;p++;while(*p==' ')p++;if(*p)st[ns++]=p;else break;p=strchr(p,'|');while(p&&p[1]=='|')p=strchr(p+2,'|');}\n"
"    if(ns>=2){int pr=-1;pid_t pids[16];\n"
"      for(int i=0;i<ns;i++){int pfd[2];if(i<ns-1)pipe(pfd);pid_t pid=fork();\n"
"        if(pid==0){if(pr>=0){dup2(pr,0);close(pr);}if(i<ns-1){close(pfd[0]);dup2(pfd[1],1);close(pfd[1]);}\n"
"          char*exp=expand_vars(st[i]);char*tk[64];int nt=0;char*t=strtok(exp,\" \\t\");\n"
"          while(t&&nt<63){tk[nt++]=t;t=strtok(NULL,\" \\t\");}tk[nt]=NULL;\n"
"          if(nt>0){int rc=try_builtin(nt,tk);if(rc==-999)_exit(0);if(rc<0)exec_external(nt,tk);}\n"
"          fflush(stdout);_exit(0);}\n"
"        pids[i]=pid;if(pr>=0)close(pr);if(i<ns-1){close(pfd[1]);pr=pfd[0];}}\n"
"      if(pr>=0)close(pr);for(int i=0;i<ns;i++)waitpid(pids[i],NULL,0);return 0;}}\n"
"  char*and=strstr(line,\"&&\"),*or=strstr(line,\"||\");\n"
"  if(and||or){char*op=(and&&(!or||and<or))?and:or;int ia=(op==and);*op=0;char*rest=op+2;while(*rest==' ')rest++;int rc=execute_line(line);if(ia){if(rc==0)return execute_line(rest);return rc;}else{if(rc!=0)return execute_line(rest);return 0;}}\n"
"  int sin=-1,sout=-1,serr=-1;\n"
"  char*m=strstr(line,\"2>&1\");if(m){*m=0;serr=dup(2);dup2(1,2);}\n"
"  char*gt2=strstr(line,\" >> \");if(gt2){*gt2=0;char*fn=gt2+4;char*sp=strchr(fn,' ');if(sp)*sp=0;int fd=open(fn,O_WRONLY|O_CREAT|O_APPEND,0644);if(fd>=0){sout=dup(1);dup2(fd,1);close(fd);}else perror(fn);}\n"
"  else{char*gt=strstr(line,\" > \");if(!gt){gt=strstr(line,\">\");if(gt&&(gt==line||gt[-1]==' '||gt[-1]=='2')){}else gt=NULL;}if(gt){*gt=0;gt++;if(*gt=='>')gt++;while(*gt==' ')gt++;char*fn=gt;char*sp=strchr(fn,' ');if(sp)*sp=0;int fd=open(fn,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0){sout=dup(1);dup2(fd,1);close(fd);}else perror(fn);}}\n"
"  char*lt=strstr(line,\" < \");if(!lt){lt=strstr(line,\"<\");if(lt&&(lt==line||lt[-1]==' ')){}else lt=NULL;}if(lt){*lt=0;lt++;while(*lt==' ')lt++;char*fn=lt;char*sp=strchr(fn,' ');if(sp)*sp=0;int fd=open(fn,O_RDONLY);if(fd>=0){sin=dup(0);dup2(fd,0);close(fd);}else perror(fn);}\n"
"  char*e2=strstr(line,\" 2> \");if(!e2){e2=strstr(line,\"2>\");if(e2&&(e2==line||e2[-1]==' ')){}else e2=NULL;}if(e2){*e2=0;e2+=2;while(*e2==' ')e2++;char*fn=e2;char*sp=strchr(fn,' ');if(sp)*sp=0;int fd=open(fn,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0){if(serr<0)serr=dup(2);dup2(fd,2);close(fd);}else perror(fn);}\n"
"  char*exp=expand_vars(line);\n"
"  char*tk[64];int nt=0;char*t=strtok(exp,\" \\t\");\n"
"  while(t&&nt<63){tk[nt++]=t;t=strtok(NULL,\" \\t\");}tk[nt]=NULL;\n"
"  if(nt>0){int rc=try_builtin(nt,tk);if(rc==-999)last_rc=-999;else if(rc>=0)last_rc=rc;else last_rc=exec_external(nt,tk);}\n"
"  fflush(stdout);fflush(stderr);\n"
"  if(sin>=0){dup2(sin,0);close(sin);}if(sout>=0){dup2(sout,1);close(sout);}if(serr>=0){dup2(serr,2);close(serr);}\n"
"  return last_rc;\n"
"}\n"
"\n"
"int main(int argc,char **argv){\n"
"  setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);\n"
"  setenv(\"SHELL\",\"/bin/sh\",1);setenv(\"TERM\",\"xterm-256color\",1);setenv(\"LANG\",\"C.UTF-8\",1);\n"
"  if(!getenv(\"HOME\")||!*getenv(\"HOME\")){char hm[256];snprintf(hm,sizeof(hm),\"/home/%%s\",getenv(\"USER\")?getenv(\"USER\"):\"root\");setenv(\"HOME\",hm,1);}\n"
"  setenv(\"IFS\",\" \\t\\n\",1);\n"
"  printf(\"\\n========================================\\n\");\n"
"  printf(\"  Self-Bootstrapping Terminal\\n\");\n"
"  printf(\"  Project: %%s\\n\",PROJECT);\n"
"  printf(\"========================================\\n\\n\");\n"
"  printf(\"[1/4] Checking build tools...\\n\");\n"
"  int ng=!tool_exists(\"gcc\"),ni=!tool_exists(\"git\"),nm=!tool_exists(\"make\");\n"
"  if(ng||ni||nm){char pk[512]=\"\";if(ng)strcat(pk,\"gcc \");if(ni)strcat(pk,\"git \");if(nm)strcat(pk,\"make \");int pl=strlen(pk);if(pl>0&&pk[pl-1]==' ')pk[pl-1]=0;printf(\"[*] Installing: %%s\\n\",pk);if(pkg_install(pk)!=0){fprintf(stderr,\"[!] Failed.\\n\");return 1;}}\n"
"  else printf(\"  [OK] gcc, git, make present.\\n\");\n"
"  if(!tool_exists(\"gcc\")){fprintf(stderr,\"[!] gcc not available.\\n\");return 1;}\n"
"  printf(\"\\n[2/4] Getting project source...\\n\");\n"
"  char pd[512];snprintf(pd,sizeof(pd),\"./%%s\",PROJECT);\n"
"  if(access(pd,F_OK)==0){printf(\"  [~] Updating...\\n\");chdir(pd);__sh_exec_cmd(\"git stash\");__sh_exec_cmd(\"git pull\");chdir(\"..\");}\n"
"  else{char c[1024];snprintf(c,sizeof(c),\"git clone %%s %%s\",REPO_URL,pd);printf(\"  [+] Cloning...\\n\");if(run(c)!=0){fprintf(stderr,\"[!] Clone failed.\\n\");return 1;}}\n"
"  printf(\"\\n[3/4] Compiling project...\\n\");\n"
"  {chdir(pd);if(__sh_exec_cmd(\"make clean\")!=0||__sh_exec_cmd(\"make\")!=0){fprintf(stderr,\"[!] Compile failed.\\n\");return 1;}chdir(\"..\");}\n"
"  char bin[1024];snprintf(bin,sizeof(bin),\"%%s/shell2c\",pd);\n"
"  if(access(bin,X_OK)!=0){fprintf(stderr,\"[!] shell2c not found.\\n\");return 1;}\n"
"  printf(\"  [OK] shell2c compiled.\\n\");\n"
"  printf(\"\\n[4/4] Terminal ready.\\n\");\n"
"  printf(\"========================================\\n\");\n"
"  printf(\"  shell2c: ./%%s/shell2c\\n\",PROJECT);\n"
"  printf(\"  'help' for commands, 'exit' to quit.\\n\");\n"
"  printf(\"========================================\\n\\n\");\n"
"  char line[8192];int last_rc=0;\n"
"  while(1){\n"
"    char cwd[1024];getcwd(cwd,sizeof(cwd));const char*home=getenv(\"HOME\");\n"
"    char cd[1024];if(home&&!strncmp(cwd,home,strlen(home)))snprintf(cd,sizeof(cd),\"~%%s\",cwd+strlen(home));else snprintf(cd,sizeof(cd),\"%%s\",cwd);\n"
"    printf(\"%%s@terminal:%%s$ \",getenv(\"USER\")?getenv(\"USER\"):\"user\",cd);fflush(stdout);\n"
"    if(!fgets(line,sizeof(line),stdin))break;\n"
"    int l=strlen(line);while(l>0&&(line[l-1]=='\\n'||line[l-1]=='\\r'))line[--l]=0;\n"
"    if(!l)continue;\n"
"    int rc=execute_line(line);\n"
"    if(rc==-999)break;\n"
"    last_rc=rc;\n"
"  }\n"
"  printf(\"\\nGoodbye.\\n\");\n"
"  return last_rc;\n"
"}\n",
        repo_url, project_name);

    fclose(f);

    printf("[OK] Generated: %s\n", output_file);
    printf("\nCompile with:\n");
    printf("  gcc -O2 -o terminal %s\n", output_file);
    printf("\nRun:\n");
    printf("  ./terminal\n");
    printf("\nThe terminal will:\n");
    printf("  1. Auto-install gcc, git, make (if missing)\n");
    printf("  2. Clone %s\n", repo_url);
    printf("  3. Compile the project\n");
    printf("  4. Give you a shell to use shell2c\n");
    printf("\nCustom options:\n");
    printf("  -o <file.c>    Output C file name (default: terminal.c)\n");
    printf("  -r <url>       Git repo URL (default: %s)\n", repo_url);
    printf("  -n <name>      Project directory name (default: %s)\n", project_name);

    return 0;
}


int main(int argc, char **argv){
    /* Check for -all mode: package entire project into standalone terminal */
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-all") || !strcmp(argv[i],"--all")){
            return shell2c_all_mode(argc, argv);
        }
    }

    if(argc < 3){
        fprintf(stderr,
            "shell2c — Shell-to-C Transpiler (modular + obfuscation + VM)\n\n"
            "Author: 爱摸鱼的狐狸 🦊\n\n"
            "Usage: %s input.sh output.c [options]\n\n"
            "Options:\n"
            "  --makefile   Also emit a Makefile for the output\n"
            "  --run        Compile and run the output immediately\n"
            "  --obfuscate  Generate obfuscated C code (anti-analysis)\n"
            "  --vm         Generate VM-protected output (bytecode + hybrid JIT)\n"
            "  -all         Generate a self-bootstrapping terminal C file.\n"
            "               The compiled terminal auto-installs gcc/git, clones\n"
            "               the project repo, compiles it, and provides a shell.\n"
            "               Options: -o <file.c> -r <repo_url> -n <project_name>\n",
            argv[0]);
        return 1;
    }
    /* Run transpiler in a thread with 128MB stack to handle deep nesting */
    g_argc = argc;
    g_argv = argv;
    g_retcode = 1;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t stack_sz = 128 * 1024 * 1024; /* 128MB */
    pthread_attr_setstacksize(&attr, stack_sz);
    pthread_t tid;
    if(pthread_create(&tid, &attr, transpile_thread, NULL) != 0){
        pthread_attr_destroy(&attr);
        /* Fallback: run directly */
        fprintf(stderr, "Warning: pthread_create failed, running with default stack\n");
        /* Direct call not possible since transpile_thread reads globals;
         * emulate by calling with a large stack via ulimit */
        return 1;
    }
    pthread_attr_destroy(&attr);
    pthread_join(tid, NULL);
    return g_retcode;
}
