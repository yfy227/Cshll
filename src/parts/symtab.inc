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
    "on_exit","atexit","signal","system","popen","pclose","tmpfile","fopen","fclose",
    "fread","fwrite","fgetc","fputc","fputs","fgets","clearerr","fileno","fseek",
    "ftell","rewind","remove","rename","time","srand","rand","qsort","sort",
    NULL
};

/* Override safe_cname to use extended keyword list.
 * Uses a rotating pool of buffers so multiple calls in one expression
 * (e.g. fprintf(out, "%s...%s", safe_cname(a), safe_cname(b)))
 * don't clobber each other. */
#define CNAME_POOL_SIZE 8
static char _cname_pool[CNAME_POOL_SIZE][256];
static int  _cname_idx = 0;

/* Escape a string for use inside a C double-quoted string literal.
 * Returns a newly allocated string. Handles: \ " and control chars.
 * Does NOT add surrounding quotes. */
static char *c_escape_literal(const char *s){
    if(!s) return xstrdup("");
    size_t len = strlen(s);
    char *r = malloc(len * 4 + 1);
    int j = 0;
    for(int i = 0; s[i]; i++){
        switch(s[i]){
            case '\\': r[j++]='\\'; r[j++]='\\'; break;
            case '"':  r[j++]='\\'; r[j++]='"';  break;
            case '\n': r[j++]='\\'; r[j++]='n';  break;
            case '\t': r[j++]='\\'; r[j++]='t';  break;
            case '\r': r[j++]='\\'; r[j++]='r';  break;
            default:   r[j++]=s[i]; break;
        }
    }
    r[j] = 0;
    return r;
}

const char *safe_cname(const char *name){
    char *buf = _cname_pool[_cname_idx];
    _cname_idx = (_cname_idx + 1) % CNAME_POOL_SIZE;
    if(!name||!*name){ snprintf(buf,256,"__sh_empty"); return buf; }
    for(int i=0;C_KEYWORDS_EXT[i];i++){
        if(strcmp(name,C_KEYWORDS_EXT[i])==0){
            snprintf(buf,256,"_sh_%s",name);
            return buf;
        }
    }
    if(!isalpha((unsigned char)name[0]) && name[0]!='_'){
        snprintf(buf,256,"_sh_%s",name);
        return buf;
    }
    int dirty=0;
    for(const char *p=name;*p;p++){
        if(!isalnum((unsigned char)*p) && *p!='_'){ dirty=1; break; }
    }
    if(dirty){
        int j=0;
        for(const char *p=name;*p && j<255;p++){
            buf[j++] = (isalnum((unsigned char)*p)||*p=='_') ? *p : '_';
        }
        buf[j]=0;
        return buf;
    }
    /* name is clean — mangle if obfuscate mode */
    extern int g_obfuscate;
    if(g_obfuscate){
        extern const char *mangle(const char*);
        const char *mangled = mangle(name);
        if(mangled != name) return mangled;
        /* If mangle didn't change it (shouldn't happen for clean names), apply FNV hash */
        unsigned int h = 2166136261u;
        for(const char *p=name; *p; p++){ h ^= (unsigned char)*p; h *= 16777619u; }
        snprintf(buf, 256, "_Q%08x", h);
        return buf;
    }
    return name;
}

/* ================================================================== */
/* L2  Symbol table — types in s2c_symtab.h, definitions here          */
/* ================================================================== */

/* VarKind, VarInfo, var_table, var_count declared in s2c_symtab.h */
VarInfo var_table[MAX_VARS];
int var_count = 0;

/* ── Hash table for O(1) variable lookup ── */
#define VAR_HASH_SIZE 65536  /* power of 2, mask with VAR_HASH_SIZE-1 */
#define VAR_HASH_MASK (VAR_HASH_SIZE - 1)
static int var_hash_buckets[VAR_HASH_SIZE]; /* index into var_table, -1 = empty */
static int var_hash_next[VAR_HASH_SIZE];   /* chaining for collisions */

static unsigned int var_hash(const char *name){
    /* FNV-1a hash — fast, good distribution for short strings */
    unsigned int h = 2166136261u;
    while(*name){
        h ^= (unsigned char)*name++;
        h *= 16777619u;
    }
    return h & VAR_HASH_MASK;
}

static int var_hash_initialized = 0;

static void var_hash_init(void){
    memset(var_hash_buckets, -1, sizeof(var_hash_buckets));
    memset(var_hash_next, -1, sizeof(var_hash_next));
    var_hash_initialized = 1;
}

static int var_hash_find(const char *name){
    if(!var_hash_initialized) var_hash_init();
    unsigned int h = var_hash(name);
    int idx = var_hash_buckets[h];
    while(idx >= 0){
        if(strcmp(var_table[idx].name, name) == 0) return idx;
        idx = var_hash_next[idx];
    }
    return -1;
}

static void var_hash_insert(int idx){
    unsigned int h = var_hash(var_table[idx].name);
    var_hash_next[idx] = var_hash_buckets[h];
    var_hash_buckets[h] = idx;
}

void add_var(const char *name, VarKind k){
    if(!name||!*name) return;
    int idx = var_hash_find(name);
    if(idx >= 0){
        if(k==V_ASSOC) var_table[idx].kind=V_ASSOC;
        else if(k==V_ARRAY && var_table[idx].kind!=V_ASSOC) var_table[idx].kind=V_ARRAY;
        else if(k==V_INT) var_table[idx].kind=V_INT;
        else if(k==V_STR && var_table[idx].kind==V_INT) var_table[idx].kind=V_STR;
        return;
    }
    if(var_count>=MAX_VARS) return;
    strncpy(var_table[var_count].name,name,127);
    var_table[var_count].name[127]=0;
    var_table[var_count].kind=k;
    var_table[var_count].is_local=0;
    var_table[var_count].is_decl_int=0;
    var_hash_insert(var_count);
    var_count++;
}
VarKind get_var_kind(const char *name){
    if(!name) return V_STR;
    int idx = var_hash_find(name);
    return idx >= 0 ? var_table[idx].kind : V_STR;
}
int is_known_var(const char *name){
    if(!name) return 0;
    return var_hash_find(name) >= 0;
}
int is_declared_int(const char *name){
    if(!name) return 0;
    int idx = var_hash_find(name);
    return idx >= 0 ? var_table[idx].is_decl_int : 0;
}
void set_declared_int(const char *name){
    if(!name||!*name) return;
    int idx = var_hash_find(name);
    if(idx >= 0){
        var_table[idx].is_decl_int=1;
        return;
    }
    /* not found — create as V_INT with decl_int flag */
    if(var_count>=MAX_VARS) return;
    strncpy(var_table[var_count].name,name,127);
    var_table[var_count].name[127]=0;
    var_table[var_count].kind=V_INT;
    var_table[var_count].is_local=0;
    var_table[var_count].is_decl_int=1;
    var_hash_insert(var_count);
    var_count++;
}
const char *var_c_expr(const char *name){
    if(is_known_var(name)) return safe_cname(name);
    static char buf[256];
    snprintf(buf,sizeof(buf),"(__sh_getenv(\"%s\"))",name);
    return buf;
}

/* Function table — with hash index for O(1) lookup */
char *func_table[MAX_FUNCS];
int func_count = 0;
#define FUNC_HASH_SIZE 4096
#define FUNC_HASH_MASK (FUNC_HASH_SIZE - 1)
static int func_hash_buckets[FUNC_HASH_SIZE];
static int func_hash_next[MAX_FUNCS];
static int func_hash_initialized = 0;

static unsigned int func_hash(const char *name){
    unsigned int h = 2166136261u;
    while(*name){ h ^= (unsigned char)*name++; h *= 16777619u; }
    return h & FUNC_HASH_MASK;
}

void register_func(const char *name){
    if(!name||!*name) return;
    if(!func_hash_initialized){
        memset(func_hash_buckets, -1, sizeof(func_hash_buckets));
        func_hash_initialized = 1;
    }
    unsigned int h = func_hash(name);
    int idx = func_hash_buckets[h];
    while(idx >= 0){
        if(strcmp(func_table[idx],name)==0) return;
        idx = func_hash_next[idx];
    }
    if(func_count<MAX_FUNCS){
        func_table[func_count]=xstrdup(name);
        func_hash_next[func_count] = func_hash_buckets[h];
        func_hash_buckets[h] = func_count;
        func_count++;
    }
}
int is_user_func(const char *name){
    if(!name||!*name) return 0;
    if(!func_hash_initialized) return 0;
    unsigned int h = func_hash(name);
    int idx = func_hash_buckets[h];
    while(idx >= 0){
        if(strcmp(func_table[idx],name)==0) return 1;
        idx = func_hash_next[idx];
    }
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
int heredoc_peek(void){
    return heredoc_next < heredoc_count;
}
const char *heredoc_consume(int *expand){
    if(heredoc_next>=heredoc_count){ if(expand)*expand=1; return ""; }
    if(expand) *expand = heredoc_table[heredoc_next].expand;
    return heredoc_table[heredoc_next++].text;
}

