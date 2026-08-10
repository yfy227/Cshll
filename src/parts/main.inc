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
 * shell2c becomes a complete shell with env, path, apt/pkg support
 * ============================================================ */

static void init_terminal_env(void){
    /* Complete environment variable setup */
    setenv("SHELL","/bin/sh",1);
    setenv("TERM","xterm-256color",1);
    setenv("LANG","C.UTF-8",1);
    setenv("LC_ALL","C.UTF-8",1);
    setenv("PS1","\\u@\\h:\\w$ ",1);
    setenv("PROMPT_COMMAND","",1);

    /* Ensure PATH includes all standard tool locations */
    const char *path=getenv("PATH");
    if(!path || !*path){
        setenv("PATH",
            "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:"
            "/sbin:/bin:/usr/games:/usr/local/games:"
            "/opt/local/bin:/opt/bin:"
            "/snap/bin:/home/"
            ,1);
    } else {
        /* Append missing standard paths */
        char newpath[4096];
        const char *extra[]={
            "/usr/local/sbin","/usr/local/bin","/usr/sbin","/usr/bin",
            "/sbin","/bin","/snap/bin",NULL
        };
        snprintf(newpath,sizeof(newpath),"%s",path);
        for(int i=0;extra[i];i++){
            if(!strstr(path,extra[i])){
                strncat(newpath,":",sizeof(newpath)-strlen(newpath)-1);
                strncat(newpath,extra[i],sizeof(newpath)-strlen(newpath)-1);
            }
        }
        setenv("PATH",newpath,1);
    }

    /* Ensure HOME is set */
    if(!getenv("HOME") || !*getenv("HOME")){
        char hm[256];
        snprintf(hm,sizeof(hm),"/home/%s",getenv("USER")?getenv("USER"):"root");
        setenv("HOME",hm,1);
    }

    /* Set default IFS */
    setenv("IFS"," \t\n",1);
}

static int is_pkg_tool(const char *cmd){
    return !strcmp(cmd,"apt")||!strcmp(cmd,"apt-get")||!strcmp(cmd,"apt-cache")||
           !strcmp(cmd,"pkg")||!strcmp(cmd,"dpkg")||!strcmp(cmd,"snap")||
           !strcmp(cmd,"pip")||!strcmp(cmd,"pip3")||!strcmp(cmd,"npm")||
           !strcmp(cmd,"cargo")||!strcmp(cmd,"gem")||!strcmp(cmd,"yarn")||
           !strcmp(cmd,"brew")||!strcmp(cmd,"conda")||!strcmp(cmd,"nix");
}

static int is_shell_builtin(const char *cmd){
    return !strcmp(cmd,"cd")||!strcmp(cmd,"pwd")||!strcmp(cmd,"echo")||
           !strcmp(cmd,"export")||!strcmp(cmd,"unset")||!strcmp(cmd,"set")||
           !strcmp(cmd,"source")||!strcmp(cmd,"alias")||!strcmp(cmd,"history")||
           !strcmp(cmd,"exit")||!strcmp(cmd,"quit")||!strcmp(cmd,"help")||
           !strcmp(cmd,"env")||!strcmp(cmd,"which")||!strcmp(cmd,"type")||
           !strcmp(cmd,"read")||!strcmp(cmd,"eval")||!strcmp(cmd,"exec");
}

static int run_shell_builtin(int argc, char **argv){
    if(!strcmp(argv[0],"cd")){
        const char *dir=argc>1?argv[1]:getenv("HOME");
        if(!dir) dir="/";
        if(chdir(dir)!=0){ perror("cd"); return 1; }
        char cwd[1024]; getcwd(cwd,sizeof(cwd));
        setenv("PWD",cwd,1);
        return 0;
    }
    if(!strcmp(argv[0],"pwd")){
        char cwd[1024]; getcwd(cwd,sizeof(cwd));
        printf("%s\n",cwd);
        return 0;
    }
    if(!strcmp(argv[0],"echo")){
        for(int i=1;i<argc;i++){
            if(i>1) putchar(' ');
            /* Expand $VAR in args, strip outer quotes */
            const char *s=argv[i];
            int slen=strlen(s);
            /* Strip outer double quotes */
            if(slen>=2 && s[0]=='"' && s[slen-1]=='"'){
                s++; slen-=2;
            }
            while(*s && slen>0){
                if(*s=='$' && s[1]){
                    char vn[128]; int vi=0; s++; slen--;
                    if(*s=='{'){ s++; slen--; while(*s&&*s!='}'&&vi<127){ vn[vi++]=*s++; slen--; } if(*s){s++; slen--;} }
                    else { while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127){ vn[vi++]=*s++; slen--; } }
                    vn[vi]=0;
                    const char *val=getenv(vn);
                    if(val) fputs(val,stdout);
                } else { putchar(*s); s++; slen--; }
            }
        }
        putchar('\n');
        return 0;
    }
    if(!strcmp(argv[0],"export")){
        if(argc<2){ /* print all env */ extern char **environ; for(char **e=environ;*e;e++) printf("%s\n",*e); return 0; }
        for(int i=1;i<argc;i++){
            char *eq=strchr(argv[i],'=');
            if(eq){ *eq=0; setenv(argv[i],eq+1,1); }
            else { /* export VAR (no assignment) — just mark */ }
        }
        return 0;
    }
    if(!strcmp(argv[0],"unset")){
        for(int i=1;i<argc;i++) unsetenv(argv[i]);
        return 0;
    }
    if(!strcmp(argv[0],"env")){
        extern char **environ;
        for(char **e=environ;*e;e++) printf("%s\n",*e);
        return 0;
    }
    if(!strcmp(argv[0],"which")||!strcmp(argv[0],"type")){
        if(argc<2) return 1;
        char path[4096]; snprintf(path,sizeof(path),"%s",getenv("PATH"));
        char *tok=strtok(path,":");
        while(tok){
            char fp[1024]; snprintf(fp,sizeof(fp),"%s/%s",tok,argv[1]);
            if(access(fp,X_OK)==0){ printf("%s\n",fp); return 0; }
            tok=strtok(NULL,":");
        }
        fprintf(stderr,"%s: not found\n",argv[1]);
        return 1;
    }
    if(!strcmp(argv[0],"alias")){
        if(argc<2){ system("alias 2>/dev/null || true"); return 0; }
        /* Store alias as env var with __alias_ prefix */
        char an[256]; snprintf(an,sizeof(an),"__alias_%s",argv[1]);
        if(argc>2) setenv(an,argv[2],1);
        return 0;
    }
    if(!strcmp(argv[0],"history")){
        system("history 2>/dev/null || cat ~/.bash_history 2>/dev/null || true");
        return 0;
    }
    if(!strcmp(argv[0],"source")||!strcmp(argv[0],".")){
        if(argc<2) return 1;
        char cmd[1024]; snprintf(cmd,sizeof(cmd),"source %s 2>/dev/null || . %s",argv[1],argv[1]);
        return system(cmd);
    }
    if(!strcmp(argv[0],"eval")){
        char cmd[4096]="";
        for(int i=1;i<argc;i++){ if(i>1) strcat(cmd," "); strncat(cmd,argv[i],sizeof(cmd)-strlen(cmd)-1); }
        return system(cmd);
    }
    if(!strcmp(argv[0],"exec")){
        /* exec: replace current process */
        if(argc>1) execvp(argv[1],argv+1);
        return 0;
    }
    if(!strcmp(argv[0],"set")){
        if(argc<2){ extern char **environ; for(char **e=environ;*e;e++) printf("%s\n",*e); return 0; }
        /* set VAR=val */
        for(int i=1;i<argc;i++){
            char *eq=strchr(argv[i],'=');
            if(eq){ *eq=0; setenv(argv[i],eq+1,1); }
        }
        return 0;
    }
    if(!strcmp(argv[0],"exit")||!strcmp(argv[0],"quit")) return -999;
    if(!strcmp(argv[0],"help")){
        printf("shell2c Interactive Shell — Built-in Commands:\n");
        printf("  cd [dir]       Change directory\n");
        printf("  pwd            Print working directory\n");
        printf("  echo [args]    Print arguments ($VAR expansion)\n");
        printf("  export VAR=val Set environment variable\n");
        printf("  unset VAR     Remove environment variable\n");
        printf("  env           Print all environment variables\n");
        printf("  which cmd     Find command in PATH\n");
        printf("  alias         Set/view aliases\n");
        printf("  source file   Execute script file\n");
        printf("  eval cmd      Evaluate command string\n");
        printf("  exec cmd      Replace process with command\n");
        printf("  set [VAR=val] Set env or print all\n");
        printf("  history       Show command history\n");
        printf("  help          Show this help\n");
        printf("  exit          Exit shell\n\n");
        printf("Package managers: apt, apt-get, pkg, dpkg, snap, pip, npm, cargo, gem, yarn, brew, conda\n");
        printf("All system commands available via PATH.\n");
        return 0;
    }
    return -1; /* not a builtin */
}

static char *expand_vars(const char *line){
    static char buf[8192];
    int bi=0;
    const char *s=line;
    while(*s && bi<(int)sizeof(buf)-2){
        if(*s=='$' && s[1]){
            char vn[128]; int vi=0; s++;
            if(*s=='{'){ s++; while(*s&&*s!='}'&&vi<127) vn[vi++]=*s++; if(*s)s++; }
            else if(*s=='('){ /* $(cmd) */ s++; int d=1; char cmd[2048]; int ci=0;
                while(*s&&d&&ci<2047){ if(*s=='(')d++; else if(*s==')'){d--;if(d==0){s++;break;}} cmd[ci++]=*s++; }
                cmd[ci]=0;
                FILE *p=popen(cmd,"r"); if(p){ char tb[4096]; size_t n=fread(tb,1,4095,p); tb[n>0?n:0]=0; pclose(p);
                    /* strip trailing newline */
                    while(n>0&&tb[n-1]=='\n') tb[--n]=0;
                    for(int j=0;j<(int)n&&bi<sizeof(buf)-2;j++) buf[bi++]=tb[j];
                }
                continue;
            }
            else { while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&vi<127) vn[vi++]=*s++; }
            vn[vi]=0;
            if(vi>0){
                const char *val=getenv(vn);
                if(val){ int vl=strlen(val); for(int j=0;j<vl&&bi<(int)sizeof(buf)-2;j++) buf[bi++]=val[j]; }
            } else { buf[bi++]='$'; }
            continue;
        }
        if(*s=='~' && (s==line || s[-1]==' ' || s[-1]=='\t')){
            const char *home=getenv("HOME"); if(home){ int hl=strlen(home); for(int j=0;j<hl&&bi<(int)sizeof(buf)-2;j++) buf[bi++]=home[j]; s++; continue; }
        }
        buf[bi++]=*s++;
    }
    buf[bi]=0;
    return buf;
}

static char *parse_redirections(char *line, int *stdin_fd, int *stdout_fd, int *stderr_fd){
    /* Handle >, <, 2>, >>, 2>&1, | */
    /* For simplicity, pipe and complex redirections fall through to system() */
    *stdin_fd=-1; *stdout_fd=-1; *stderr_fd=-1;
    return line;
}

int shell2c_all_mode(int argc, char **argv){
    /* Initialize complete terminal environment */
    init_terminal_env();

    /* Check if running a single command: shell2c -all -c "command" */
    const char *single_cmd=NULL;
    for(int i=1;i<argc;i++){
        if((!strcmp(argv[i],"-c")||!strcmp(argv[i],"-e")) && i+1<argc){
            single_cmd=argv[++i];
        }
    }

    if(single_cmd){
        /* Execute single command and exit */
        char *expanded=expand_vars(single_cmd);
        /* Try builtins first */
        char *toks[64]; int nt=0;
        char *tk=strtok(expanded," \t");
        while(tk && nt<63){ toks[nt++]=tk; tk=strtok(NULL," \t"); }
        toks[nt]=NULL;
        if(nt>0){
            int rc=run_shell_builtin(nt,toks);
            if(rc==-999) return 0;
            if(rc>=0) return rc;
        }
        /* Fall through to system() */
        return system(single_cmd);
    }

    /* Interactive REPL mode */
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  shell2c Interactive Terminal v1.0           ║\n");
    printf("║  Complete shell environment with package     ║\n");
    printf("║  management support (apt/pkg/dpkg/snap)     ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Environment:\n");
    printf("  USER: %s\n", getenv("USER")?getenv("USER"):"(unset)");
    printf("  HOME: %s\n", getenv("HOME"));
    printf("  SHELL: %s\n", getenv("SHELL"));
    printf("  TERM: %s\n", getenv("TERM"));
    printf("  PATH: %s\n", getenv("PATH"));
    printf("  PWD: %s\n", getenv("PWD")?getenv("PWD"):"(unset)");
    printf("\n");
    printf("Package tools: apt apt-get pkg dpkg snap pip npm cargo gem yarn brew conda\n");
    printf("Type 'help' for built-in commands, 'exit' to quit.\n\n");

    /* Read-eval loop */
    char line[8192];
    int last_rc=0;
    while(1){
        /* Print prompt */
        char cwd[1024]; getcwd(cwd,sizeof(cwd));
        const char *user=getenv("USER")?getenv("USER"):"user";
        const char *host=getenv("HOSTNAME")?getenv("HOSTNAME"):"shell2c";
        if(strlen(host)>20) host="shell2c";
        /* Shorten home in cwd */
        const char *home=getenv("HOME");
        char cwd_disp[1024];
        if(home && !strncmp(cwd,home,strlen(home))){
            snprintf(cwd_disp,sizeof(cwd_disp),"~%s",cwd+strlen(home));
        } else {
            snprintf(cwd_disp,sizeof(cwd_disp),"%s",cwd);
        }
        printf("%s@%s:%s$ ",user,host,cwd_disp);
        fflush(stdout);

        if(!fgets(line,sizeof(line),stdin)) break;
        /* Strip newline */
        int ll=strlen(line);
        while(ll>0 && (line[ll-1]=='\n'||line[ll-1]=='\r')) line[--ll]=0;
        if(!ll) continue;

        /* Expand $VAR and ~ */
        char *expanded=expand_vars(line);
        if(!expanded || !*expanded) continue;

        /* Tokenize */
        char *toks[64]; int nt=0;
        char *tk=strtok(expanded," \t");
        while(tk && nt<63){ toks[nt++]=tk; tk=strtok(NULL," \t"); }
        toks[nt]=NULL;
        if(nt==0) continue;

        /* Check for pipe — if present, use system() */
        int has_pipe=0;
        for(int i=0;i<nt;i++) if(!strcmp(toks[i],"|")){ has_pipe=1; break; }
        if(has_pipe){
            int rc=system(line);
            if(WIFEXITED(rc)) last_rc=WEXITSTATUS(rc);
            else last_rc=1;
            continue;
        }

        /* Check for && and || */
        int has_and=0,has_or=0;
        for(int i=0;i<nt;i++){
            if(!strcmp(toks[i],"&&")){ has_and=1; break; }
            if(!strcmp(toks[i],"||")){ has_or=1; break; }
        }
        if(has_and||has_or){
            int rc=system(line);
            if(WIFEXITED(rc)) last_rc=WEXITSTATUS(rc);
            else last_rc=1;
            continue;
        }

        /* Check for redirection */
        int has_redir=0;
        for(int i=0;i<nt;i++){
            if(toks[i][0]=='>'||toks[i][0]=='<'||(toks[i][0]=='2'&&toks[i][1]=='>')){
                has_redir=1; break;
            }
        }
        if(has_redir){
            int rc=system(line);
            if(WIFEXITED(rc)) last_rc=WEXITSTATUS(rc);
            else last_rc=1;
            continue;
        }

        /* Try shell builtins */
        int rc=run_shell_builtin(nt,toks);
        if(rc==-999) break; /* exit */
        if(rc>=0){ last_rc=rc; continue; }

        /* Package tools — pass through with full env */
        if(is_pkg_tool(toks[0])){
            /* Ensure package manager exists */
            int rc=system(line);
            if(WIFEXITED(rc)) last_rc=WEXITSTATUS(rc);
            else last_rc=1;
            continue;
        }

        /* Regular external command — search PATH and execute */
        char path_env[4096]; snprintf(path_env,sizeof(path_env),"%s",getenv("PATH"));
        char *ptok=strtok(path_env,":");
        int found=0;
        while(ptok){
            char fp[1024]; snprintf(fp,sizeof(fp),"%s/%s",ptok,toks[0]);
            if(access(fp,X_OK)==0){ found=1; break; }
            ptok=strtok(NULL,":");
        }
        if(!found){
            fprintf(stderr,"%s: command not found\n",toks[0]);
            last_rc=127;
            continue;
        }

        /* Execute via system() for complex commands, or fork+exec for simple ones */
        pid_t pid=fork();
        if(pid==0){
            /* Child */
            execvp(toks[0],toks);
            /* If execvp fails, try system() as fallback */
            fprintf(stderr,"%s: execution failed\n",toks[0]);
            _exit(127);
        } else if(pid>0){
            int status;
            waitpid(pid,&status,0);
            if(WIFEXITED(status)) last_rc=WEXITSTATUS(status);
            else last_rc=1;
        } else {
            /* fork failed — fallback to system() */
            int rc=system(line);
            if(WIFEXITED(rc)) last_rc=WEXITSTATUS(rc);
            else last_rc=1;
        }
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
