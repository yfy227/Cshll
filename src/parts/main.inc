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
 * -all mode: Package all .sh files into a standalone terminal
 * ============================================================ */

/* Scan directory for .sh files */
static int scan_sh_files(const char *dir, char files[][512], int max){
    DIR *dp=opendir(dir);
    if(!dp) return 0;
    int n=0;
    struct dirent *e;
    while((e=readdir(dp)) && n<max){
        int len=strlen(e->d_name);
        if(len>3 && !strcmp(e->d_name+len-3,".sh")){
            snprintf(files[n],512,"%s/%s",dir,e->d_name);
            n++;
        }
    }
    closedir(dp);
    return n;
}

int shell2c_all_mode(int argc, char **argv){
    fprintf(stderr,"shell2c -all — Standalone Terminal Packaging Mode\n");
    fprintf(stderr,"=================================================\n\n");

    /* Determine output binary name */
    const char *outname = "terminal";
    for(int i=1;i<argc;i++){
        if((!strcmp(argv[i],"-o")||!strcmp(argv[i],"--output")) && i+1<argc){
            outname=argv[++i];
        }
    }

    /* Scan current directory for .sh files */
    char sh_files[64][512];
    int nfiles=scan_sh_files(".",sh_files,64);

    if(nfiles==0){
        fprintf(stderr,"No .sh files found in current directory.\n");
        fprintf(stderr,"Place your shell scripts in the current directory and run shell2c -all again.\n");
        return 1;
    }

    fprintf(stderr,"Found %d script(s):\n",nfiles);
    for(int i=0;i<nfiles;i++){
        fprintf(stderr,"  [%d] %s\n",i+1,sh_files[i]);
    }
    fprintf(stderr,"\n");

    /* Create a temporary build directory */
    mkdir("/tmp/s2c_all_build",0755);

    /* Generate the main terminal program */
    char main_c_path[512];
    snprintf(main_c_path,512,"/tmp/s2c_all_build/terminal_main.c");
    FILE *fm=fopen(main_c_path,"w");
    if(!fm){ perror(main_c_path); return 1; }

    /* Emit the full runtime + interactive menu */
    fprintf(fm,"/* Standalone Terminal — generated by shell2c -all */\n");
    fprintf(fm,"#include <stdio.h>\n");
    fprintf(fm,"#include <stdlib.h>\n");
    fprintf(fm,"#include <string.h>\n");
    fprintf(fm,"#include <unistd.h>\n");
    fprintf(fm,"#include <sys/wait.h>\n\n");

    /* Embed script names and contents */
    fprintf(fm,"/* ---- Embedded scripts ---- */\n");
    for(int i=0;i<nfiles;i++){
        /* Read file content */
        FILE *sf=fopen(sh_files[i],"r");
        if(!sf) continue;
        fseek(sf,0,SEEK_END);
        long sz=ftell(sf);
        fseek(sf,0,SEEK_SET);
        char *content=malloc(sz+1);
        fread(content,1,sz,sf);
        content[sz]=0;
        fclose(sf);

        fprintf(fm,"static const char *__script_%d =\n",i);
        for(const char *p=content;*p;p++){
            fputc('"',fm);
            for(;*p && *p!='\n';p++){
                if(*p=='"'||*p=='\\') fputc('\\',fm);
                fputc(*p,fm);
            }
            fprintf(fm,"\\n\"\n");
            if(!*p) break;
        }
        fprintf(fm,";\n\n");
        free(content);
    }

    /* Emit menu */
    fprintf(fm,"int main(int argc, char **argv){\n");

    /* Check for direct run mode: ./terminal 1 → run script 1 (no menu) */
    fprintf(fm,"    if(argc>1){\n");
    fprintf(fm,"        int sel=atoi(argv[1]);\n");
    fprintf(fm,"        if(sel<1||sel>%d){fprintf(stderr,\"Invalid selection\\n\");return 1;}\n",nfiles);
    fprintf(fm,"        char tmpf[256]; snprintf(tmpf,sizeof(tmpf),\"/tmp/s2c_run_%%d.sh\",getpid());\n");
    fprintf(fm,"        FILE *tf=fopen(tmpf,\"w\");\n");
    fprintf(fm,"        if(!tf){perror(tmpf);return 1;}\n");
    for(int i=0;i<nfiles;i++){
        fprintf(fm,"        if(sel==%d){fputs(__script_%d,tf);}\n",i+1,i);
    }
    fprintf(fm,"        fclose(tf);\n");
    fprintf(fm,"        /* Execute via system() — this runs in a real shell */\n");
    fprintf(fm,"        char cmd[512]; snprintf(cmd,sizeof(cmd),\"bash \\\"%%s\\\"\",tmpf);\n");
    fprintf(fm,"        int rc=system(cmd);\n");
    fprintf(fm,"        unlink(tmpf);\n");
    fprintf(fm,"        if(WIFEXITED(rc)) return WEXITSTATUS(rc);\n");
    fprintf(fm,"        return 1;\n");
    fprintf(fm,"    }\n\n");

    /* Interactive menu mode — print banner */
    fprintf(fm,"    printf(\"\\n\");\n");
    fprintf(fm,"    printf(\"========================================\\n\");\n");
    fprintf(fm,"    printf(\"  Standalone Terminal (shell2c -all)   \\n\");\n");
    fprintf(fm,"    printf(\"========================================\\n\");\n");
    fprintf(fm,"    printf(\"\\n\");\n");
    fprintf(fm,"    printf(\"Embedded scripts:\\n\");\n");
    for(int i=0;i<nfiles;i++){
        { char _bn[600]; snprintf(_bn,sizeof(_bn),"    printf(\"  %%d. %%s\\n\",%d,\"%s\");\n",i+1,sh_files[i]); fputs(_bn,fm); }
    }
    fprintf(fm,"    printf(\"  0. Exit\\n\");\n");
    fprintf(fm,"    printf(\"\\n\");\n");
    fprintf(fm,"    while(1){\n");
    fprintf(fm,"        printf(\"\\nSelect script [1-%d, 0=exit]: \",%d);\n",nfiles,nfiles);
    fprintf(fm,"        char buf[32];\n");
    fprintf(fm,"        if(!fgets(buf,sizeof(buf),stdin)) break;\n");
    fprintf(fm,"        int sel=atoi(buf);\n");
    fprintf(fm,"        if(sel==0) break;\n");
    fprintf(fm,"        if(sel<1||sel>%d){printf(\"Invalid selection\\n\");continue;}\n",nfiles);
    fprintf(fm,"        char tmpf[256]; snprintf(tmpf,sizeof(tmpf),\"/tmp/s2c_run_%%d.sh\",getpid());\n");
    fprintf(fm,"        FILE *tf=fopen(tmpf,\"w\");\n");
    fprintf(fm,"        if(!tf){perror(tmpf);continue;}\n");
    for(int i=0;i<nfiles;i++){
        fprintf(fm,"        if(sel==%d){fputs(__script_%d,tf);}\n",i+1,i);
    }
    fprintf(fm,"        fclose(tf);\n");
    fprintf(fm,"        char cmd[512]; snprintf(cmd,sizeof(cmd),\"bash \\\"%%s\\\"\",tmpf);\n");
    fprintf(fm,"        system(cmd);\n");
    fprintf(fm,"        unlink(tmpf);\n");
    fprintf(fm,"    }\n");
    fprintf(fm,"    printf(\"Goodbye.\\n\");\n");
    fprintf(fm,"    return 0;\n");
    fprintf(fm,"}\n");
    fclose(fm);

    fprintf(stderr,"Compiling standalone terminal...\n");

    /* Compile the terminal program */
    char compile_cmd[2048];
    snprintf(compile_cmd,sizeof(compile_cmd),
        "gcc -O2 -o %s %s -lpthread 2>&1",
        outname, main_c_path);
    int rc=system(compile_cmd);
    if(rc!=0){
        fprintf(stderr,"Compilation failed. Check errors above.\n");
        return 1;
    }

    fprintf(stderr,"\n[OK] Standalone terminal created: %s\n",outname);
    fprintf(stderr,"\nUsage:\n");
    fprintf(stderr,"  ./%s          Interactive menu mode\n",outname);
    fprintf(stderr,"  ./%s N        Directly run script #N\n",outname);
    fprintf(stderr,"\nEmbedded scripts:\n");
    for(int i=0;i<nfiles;i++){
        fprintf(stderr,"  %d. %s\n",i+1,sh_files[i]);
    }

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
            "  -all         Package all .sh files in current directory into a\n"
            "               standalone terminal program (no script processing)\n",
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
