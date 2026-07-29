/*
 * s2c_obfuscate.c — Runtime code obfuscation implementation
 * Author: 爱摸鱼的狐狸
 */
#include "s2c_obfuscate.h"
#include <string.h>
#include <stdlib.h>

/* XOR-based string decoder — emitted into generated C */
static const char *OBfuscation_runtime =
"/* ---- anti-analysis runtime ---- */\n"
"static char _xd0[4096], _xd1[4096], _xd2[4096], _xd3[4096];\n"
"static int  _xd_i = 0;\n"
"static const char *_xs(const char *e, int n){\n"
"  char *b = ( (_xd_i==0) ? _xd0 : (_xd_i==1) ? _xd1 : (_xd_i==2) ? _xd2 : _xd3 );\n"
"  _xd_i = (_xd_i + 1) & 3;\n"
"  for(int i=0;i<n;i++) b[i]=e[i]^0x5A;\n"
"  b[n]=0; return b;\n"
"}\n"
"/* indirect dispatch table */\n"
"typedef void (*_dfn)(void);\n"
"static _dfn _dt[16];\n"
"static int _di=0;\n"
"static void _reg(_dfn f){ if(_di<16) _dt[_di++]=f; }\n"
"static void _cal(int i){ if(i>=0&&i<_di) _dt[i](); }\n"
"/* opaque predicate — always true but looks data-dependent */\n"
"static int _opq(int x){ int r=x*7+13; return (r&1)||!(r&1); }\n"
"/* dead code decoy */\n"
"static void _dec(int s){ volatile int v=s; v=(v^0xAA)+((v>>3)&0x1F); (void)v; }\n"
"\n";

void emit_obfuscation_runtime(FILE *out){
    fprintf(out, "%s", OBfuscation_runtime);
}

void emit_obfuscated_string(FILE *out, const char *str, const char *var_name){
    int len = (int)strlen(str);
    fprintf(out, "    static const char _enc_%s[] = {", var_name);
    for(int i=0; i<len; i++){
        fprintf(out, "0x%02x%s", (unsigned char)(str[i] ^ S2C_XOR_KEY), i<len-1?",":"");
    }
    fprintf(out, "};\n");
    fprintf(out, "    const char *%s = _xs(_enc_%s, %d);\n", var_name, var_name, len);
}

/* Simple name mangler: produce non-obvious names from seeds */
static const char *MANGLE_CHARS = "QqWwEeRrTtYyUuIiOoPpAaSsDdFfGgHhJjKkLlZzXxCcVvBbNnMm0123456789";
#define MANGLE_NCHARS 62
#define MANGLE_POOL 4
static char mangle_buf[MANGLE_POOL][32];
static int  mangle_idx = 0;

const char *mangle_name(int seed){
    /* Generate a deterministic but non-obvious name */
    char *buf = mangle_buf[mangle_idx];
    mangle_idx = (mangle_idx + 1) % MANGLE_POOL;
    int idx = 0;
    buf[idx++] = '_';
    buf[idx++] = 'Z';
    for(int i=0; i<6 && seed>0; i++){
        buf[idx++] = MANGLE_CHARS[seed % MANGLE_NCHARS];
        seed /= MANGLE_NCHARS;
    }
    buf[idx] = 0;
    return buf;
}

void emit_decoy_block(FILE *out, int seed){
    /* Emit a realistic-looking but meaningless code block */
    fprintf(out, "    { volatile int _v%d = %d; _v%d = (_v%d ^ 0x%x) + 0x%x; (void)_v%d; }\n",
            seed, seed*17+3, seed, seed, (seed*7)&0xFF, (seed*13)&0xFF, seed);
}

void emit_flattened_block(FILE *out, const char *code, int id){
    /* Wrap code in a switch-dispatch to flatten control flow */
    fprintf(out, "    { int _s%d = 0; while(1){ switch(_s%d){\n", id, id);
    fprintf(out, "    case 0: %s _s%d=1; break;\n", code, id);
    fprintf(out, "    case 1: _s%d=2; break;\n", id);
    fprintf(out, "    case 2: goto _end%d;\n", id);
    fprintf(out, "    default: goto _end%d;\n", id);
    fprintf(out, "    } } _end%d:; }\n", id);
}
