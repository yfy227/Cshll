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
        vmc_compile_and_emit(fout, script, do_obfuscate);

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

int shell2c_all_mode(int argc, char **argv){
    init_terminal_env();
    const char *single_cmd=NULL;
    for(int i=1;i<argc;i++){ if((!strcmp(argv[i],"-c")||!strcmp(argv[i],"-e"))&&i+1<argc) single_cmd=argv[++i]; }
    if(single_cmd){ char buf[16384]; strncpy(buf,single_cmd,sizeof(buf)-1); buf[sizeof(buf)-1]=0; return execute_line(buf); }
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  shell2c Interactive Terminal v2.0           ║\n");
    printf("║  Native pipe+redirect+60 builtins           ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");
    printf("USER=%s  HOME=%s  SHELL=%s\n", getenv("USER")?getenv("USER"):"?", getenv("HOME"), getenv("SHELL"));
    printf("PATH=%s\n\n", getenv("PATH"));
    printf("Type 'help' for commands, 'exit' to quit.\n\n");
    char line[8192]; int last_rc=0;
    while(1){
        char cwd[1024]; getcwd(cwd,sizeof(cwd));
        const char *home=getenv("HOME");
        char cd[1024];
        if(home&&!strncmp(cwd,home,strlen(home))) snprintf(cd,sizeof(cd),"~%s",cwd+strlen(home));
        else snprintf(cd,sizeof(cd),"%s",cwd);
        printf("%s@shell2c:%s$ ", getenv("USER")?getenv("USER"):"user", cd);
        fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)) break;
        int l=strlen(line); while(l>0&&(line[l-1]=='\n'||line[l-1]=='\r'))line[--l]=0;
        if(!l) continue;
        int rc=execute_line(line);
        if(rc==-999) break;
        last_rc=rc;
    }
    printf("\n");
    return last_rc;
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
            "  -all         Interactive terminal mode — shell2c becomes a complete\n"
            "               shell interpreter with full env, PATH, apt/pkg support\n",
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
