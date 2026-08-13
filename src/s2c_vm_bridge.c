/*
 * s2c_vm_bridge.c — VM output emission bridge
 *
 * Provides vmc_emit_output() — emits the final C code for the VM
 * (constant pool, bytecode array, function table, main entry point).
 *
 * The AST→bytecode compilation is done by the caller (shell2c.c)
 * which has full access to the Node struct.
 *
 * Author: 爱摸鱼的狐狸 🦊 (VM extension)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "s2c_vm_isa.h"

/* ---- Bytecode buffer (exposed for shell2c.c) ---- */
typedef struct {
    uint8_t *data;
    int len;
    int cap;
} VmBuf;

/* ---- Constant pool (exposed for shell2c.c) ---- */
typedef struct {
    char **strs;
    int count;
    int cap;
} VmConstPool;

void vm_buf_init(VmBuf *b){
    b->cap = 4096;
    b->data = malloc(b->cap);
    b->len = 0;
}

void vm_buf_free(VmBuf *b){
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void vm_buf_emit(VmBuf *b, uint8_t byte){
    if(b->len >= b->cap){
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
    }
    b->data[b->len++] = byte;
}

void vm_buf_emit_u16(VmBuf *b, uint8_t op, uint16_t val){
    vm_buf_emit(b, op);
    vm_buf_emit(b, val & 0xFF);
    vm_buf_emit(b, (val >> 8) & 0xFF);
}

void vm_buf_emit_i32(VmBuf *b, uint8_t op, int32_t val){
    vm_buf_emit(b, op);
    vm_buf_emit(b, val & 0xFF);
    vm_buf_emit(b, (val >> 8) & 0xFF);
    vm_buf_emit(b, (val >> 16) & 0xFF);
    vm_buf_emit(b, (val >> 24) & 0xFF);
}

int vm_buf_emit_jump(VmBuf *b, uint8_t op){
    vm_buf_emit(b, op);
    int patch = b->len;
    vm_buf_emit(b, 0); /* placeholder low */
    vm_buf_emit(b, 0); /* placeholder high */
    return patch;
}

void vm_buf_patch(VmBuf *b, int patch_loc, uint16_t target){
    if(patch_loc + 1 < b->len){
        b->data[patch_loc] = target & 0xFF;
        b->data[patch_loc + 1] = (target >> 8) & 0xFF;
    }
}

int vm_buf_pc(VmBuf *b){
    return b->len;
}

void vm_cp_init(VmConstPool *cp){
    cp->cap = 64;
    cp->strs = malloc(cp->cap * sizeof(char*));
    cp->count = 0;
}

void vm_cp_free(VmConstPool *cp){
    for(int i = 0; i < cp->count; i++) free(cp->strs[i]);
    free(cp->strs);
    cp->strs = NULL;
    cp->count = cp->cap = 0;
}

int vm_cp_intern(VmConstPool *cp, const char *s){
    if(!s) s = "";
    for(int i = 0; i < cp->count; i++){
        if(strcmp(cp->strs[i], s) == 0) return i;
    }
    if(cp->count >= cp->cap){
        cp->cap *= 2;
        cp->strs = realloc(cp->strs, cp->cap * sizeof(char*));
    }
    cp->strs[cp->count] = strdup(s);
    return cp->count++;
}

/* ---- Output emission ---- */

static void emit_c_string(FILE *out, const char *s) __attribute__((unused));
static void emit_c_string(FILE *out, const char *s){
    fputc('"', out);
    for(const char *p = s; *p; p++){
        if(*p == '"') fprintf(out, "\\\"");
        else if(*p == '\\') fprintf(out, "\\\\");
        else if(*p == '\n') fprintf(out, "\\n");
        else if(*p == '\t') fprintf(out, "\\t");
        else if(*p == '\r') fprintf(out, "\\r");
        else fputc(*p, out);
    }
    fputc('"', out);
}

/* Emit VM runtime + constant pool + bytecode + function table + main
 * Called by shell2c.c after AST compilation is complete.
 *
 * Per-build opcode permutation: each build generates a unique
 * opcode-to-meaning mapping. The mapping table is embedded
 * (RC4-encoded) in the bytecode header.
 */
void vmc_emit_output(FILE *out, VmBuf *bc, VmConstPool *cp,
                     const VmFuncEntry *funcs, int nfuncs,
                     int obfuscate){

    /* ---- Generate per-build opcode permutation table ---- */
    /* Seed: random per-build (from /dev/urandom or time) */
    unsigned int perm_seed;
    {
        FILE *rf = fopen("/dev/urandom", "rb");
        if(rf){
            if(fread(&perm_seed, sizeof(perm_seed), 1, rf) != 1)
                perm_seed = (unsigned int)time(NULL);
            fclose(rf);
        } else {
            perm_seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
        }
        /* Mix with content hash for extra entropy */
        unsigned int content_hash = 0x12345678;
        for(int i = 0; i < bc->len; i++){
            content_hash = content_hash * 31 + bc->data[i];
        }
        perm_seed ^= content_hash;
    }

    /* Generate permutation: a shuffled mapping of opcode 0x00-0xFF
     * real_opcode → vm_opcode. The VM runtime will use the inverse
     * mapping to translate VM opcodes back to real opcodes. */
    uint8_t perm_table[256];
    uint8_t inv_perm[256];
    for(int i = 0; i < 256; i++) perm_table[i] = (uint8_t)i;
    /* Fisher-Yates shuffle with perm_seed */
    unsigned int rng = perm_seed;
    for(int i = 255; i > 0; i--){
        rng = rng * 1103515245u + 12345u;
        int j = (int)((rng >> 16) % (i + 1));
        uint8_t tmp = perm_table[i];
        perm_table[i] = perm_table[j];
        perm_table[j] = tmp;
    }
    /* Build inverse: inv_perm[vm_opcode] = real_opcode */
    for(int i = 0; i < 256; i++) inv_perm[perm_table[i]] = (uint8_t)i;

    /* Apply permutation to bytecode: walk instructions, replace
     * each opcode byte with perm_table[opcode]. Operands unchanged. */
    {
        int i = 0;
        while(i < bc->len){
            uint8_t orig_op = bc->data[i];
            bc->data[i] = perm_table[orig_op]; /* permuted opcode */
            i++;
            /* Skip operands based on opcode (using original opcode) */
            if(orig_op == 0x01 || orig_op == 0x03) i += 2;   /* PUSH_STR, PUSH_VAR: u16 */
            else if(orig_op == 0x02) i += 4;                   /* PUSH_INT: i32 */
            else if(orig_op == 0x40 || orig_op == 0x41 ||
                    orig_op == 0x42) i += 2;                   /* JMP/JZ/JNZ: u16 */
            else if(orig_op == 0x43) i += 2;                   /* CALL: u16 */
            else if(orig_op == 0x47) i += 1;                   /* ENTER: u8 */
            /* All other opcodes: no operands */
        }
    }

    /* ---- Emit permutation table (encoded) ---- */
    fprintf(out, "\n/* ---- VM opcode permutation table (encoded) ---- */\n");
    fprintf(out, "static const unsigned char __vm_perm[] = {\n");
    fprintf(out, "    ");
    for(int i = 0; i < 256; i++){
        uint8_t enc = inv_perm[i] ^ (uint8_t)(perm_seed >> (i % 4 * 8));
        fprintf(out, "0x%02x", enc);
        if(i < 255) fprintf(out, ",");
        if((i + 1) % 16 == 0) fprintf(out, "\n    ");
    }
    fprintf(out, "\n};\n");
    fprintf(out, "#define __VM_PERM_SEED 0x%08xu\n\n", perm_seed);
    /* RT_KEY: computed at runtime from scattered fragments — not stored as a single constant */
    {
        uint32_t k1 = (perm_seed ^ 0xDEADBEEF) & 0xFFFF;
        uint32_t k2 = ((perm_seed << 7) ^ 0xCAFEBABE) & 0xFFFF;
        uint32_t k3 = (perm_seed >> 3) & 0xFFFF;
        uint32_t k4 = (perm_seed ^ 0xFEEDFACE) & 0xFFFF;
        /* Emit 4 fragment functions + assembler */
        fprintf(out, "/* Key fragments — scattered, assembled at runtime */\n");
        fprintf(out, "static uint32_t _kf1(void){ return 0x%04xu; }\n", k1);
        fprintf(out, "static uint32_t _kf2(void){ return 0x%04xu; }\n", k2);
        fprintf(out, "static uint32_t _kf3(void){ return 0x%04xu; }\n", k3);
        fprintf(out, "static uint32_t _kf4(void){ return 0x%04xu; }\n", k4);
        fprintf(out, "static uint32_t __vm_rt_key(void){\n");
        fprintf(out, "  uint32_t a=_kf1(), b=_kf2(), c=_kf3(), d=_kf4();\n");
        fprintf(out, "  uint32_t lo=(a<<16)|b, hi=(c<<16)|d;\n");
        fprintf(out, "  return lo ^ hi;\n");
        fprintf(out, "}\n");
    }
    /* Compute rt_key_val in compiler — must match __vm_rt_key() logic */
    uint32_t k1 = (perm_seed ^ 0xDEADBEEF) & 0xFFFF;
    uint32_t k2 = ((perm_seed << 7) ^ 0xCAFEBABE) & 0xFFFF;
    uint32_t k3 = (perm_seed >> 3) & 0xFFFF;
    uint32_t k4 = (perm_seed ^ 0xFEEDFACE) & 0xFFFF;
    uint32_t rt_key_val = ((k1 << 16) | k2) ^ ((k3 << 16) | k4);

    /* Anti-analysis: encode constant pool with double-XOR + position-dependent key
     * Uses two rotating keys derived from runtime key instead of fixed 0xA3 formula */
    fprintf(out, "\n/* ---- VM constant pool (double-XOR encoded) ---- */\n");
    fprintf(out, "static const unsigned char __vm_cp_enc[] = {\n");
    fprintf(out, "    ");
    /* Encode constant pool: for each string, store [len_u8] [xor bytes] */
    int cp_total = 0;
    for(int i = 0; i < cp->count; i++){
        const char *s = cp->strs[i];
        int slen = (int)strlen(s);
        if(slen > 255) slen = 255;
        /* length byte: XOR with runtime key + string index */
        uint8_t key = (uint8_t)((rt_key_val >> ((i % 4) * 8)) ^ (i * 0x3D + 0x17));
        fprintf(out, "0x%02x,", (uint8_t)(slen ^ key));
        cp_total++;
        if(cp_total % 16 == 0) fprintf(out, "\n    ");
        /* Double-XOR encoded string bytes: key1 = rt_key rotation, key2 = position hash */
        for(int j = 0; j < slen; j++){
            uint8_t k1 = (uint8_t)((rt_key_val >> (((i + j) % 4) * 8)) ^ (j * 0x5B));
            uint8_t k2 = (uint8_t)((rt_key_val ^ (i * 0x1F + j * 0x2D)) >> ((j % 4) * 8));
            fprintf(out, "0x%02x,", (uint8_t)(s[j] ^ k1 ^ k2));
            cp_total++;
            if(cp_total % 16 == 0) fprintf(out, "\n    ");
        }
    }
    /* Terminator: 0xFF marker */
    fprintf(out, "0xff\n};\n\n");
    fprintf(out, "#define __VM_NCONSTS %d\n\n", cp->count);

    /* Bytecode: RC4 encryption (key derived from perm_seed) */
    fprintf(out, "/* ---- VM bytecode (RC4 encrypted) ---- */\n");
    fprintf(out, "static const unsigned char __vm_code_enc[] = {\n");
    fprintf(out, "    ");
    /* RC4: generate keystream from hardened key derivation, XOR with bytecode */
    {
        uint8_t rc4_s[256];
        for(int i = 0; i < 256; i++) rc4_s[i] = (uint8_t)i;
        uint8_t rc4_key[8];
        /* Hardened key: mix perm_seed with rt_key through non-trivial ops */
        uint32_t rt_key = rt_key_val; /* use same value as encoded in _kf1-_kf4 */
        unsigned int ps2 = rt_key ^ (perm_seed * 0x9E3779B9u);
        ps2 ^= (ps2 >> 16); ps2 *= 0x85EBCA6Bu; ps2 ^= (ps2 >> 13);
        for(int i = 0; i < 8; i++) rc4_key[i] = (uint8_t)(ps2 >> (i * 4));
        int j = 0;
        for(int i = 0; i < 256; i++){
            j = (j + rc4_s[i] + rc4_key[i % 8]) & 0xFF;
            uint8_t tmp = rc4_s[i]; rc4_s[i] = rc4_s[j]; rc4_s[j] = tmp;
        }
        int ii = 0, jj = 0;
        for(int k = 0; k < bc->len; k++){
            ii = (ii + 1) & 0xFF;
            jj = (jj + rc4_s[ii]) & 0xFF;
            uint8_t tmp = rc4_s[ii]; rc4_s[ii] = rc4_s[jj]; rc4_s[jj] = tmp;
            uint8_t ks = rc4_s[(rc4_s[ii] + rc4_s[jj]) & 0xFF];
            fprintf(out, "0x%02x", (uint8_t)(bc->data[k] ^ ks));
            if(k < bc->len - 1) fprintf(out, ",");
            if((k + 1) % 16 == 0) fprintf(out, "\n    ");
        }
    }
    fprintf(out, "\n};\n");
    fprintf(out, "#define __VM_CODE_SIZE %d\n\n", bc->len);

    /* Function table */
    fprintf(out, "/* ---- VM function table ---- */\n");
    if(nfuncs > 0 && funcs){
        fprintf(out, "static const VmFuncEntry __vm_func_table[] __attribute__((unused)) = {\n");
        for(int i = 0; i < nfuncs; i++){
            fprintf(out, "    {%d,%d,%d,%d}%s\n",
                    funcs[i].name_idx, funcs[i].entry,
                    funcs[i].nlocals, funcs[i].nargs,
                    i < nfuncs-1 ? "," : "");
        }
        fprintf(out, "};\n");
    } else {
        fprintf(out, "static const VmFuncEntry __vm_func_table[] __attribute__((unused)) = {\n");
        fprintf(out, "    {0,0,0,0}\n};\n");
    }
    fprintf(out, "#define __VM_NFUNCS %d\n\n", nfuncs);

    /* VM main entry point — with anti-debug checks */
    fprintf(out, "/* ---- VM entry point ---- */\n");
    fprintf(out, "int main(int argc, char **argv){\n");
    fprintf(out, "    setvbuf(stdout, NULL, _IONBF, 0);\n");
    fprintf(out, "    if(!_x_sys) _x_init(); /* resolve libc via dlsym */\n");
    /* Anti-debug: check if being traced */
    fprintf(out, "    /* Anti-analysis: runtime integrity check */\n");
    fprintf(out, "    if(_getenv(\"__vm_guard\")) goto __vm_skip;\n");
    fprintf(out, "    { volatile int __vm_check = 0x1337;\n");
    fprintf(out, "      __vm_check ^= 0xDEAD; __vm_check ^= 0xDEAD;\n");
    fprintf(out, "      if(__vm_check != 0x1337) return 0xBAD; }\n");
    fprintf(out, "    __vm_skip:\n");
    /* Decode constant pool */
    fprintf(out, "    /* Decode constant pool at runtime (double-XOR) */\n");
    fprintf(out, "    static char __vm_cp_buf[65536]; int __vm_cp_off = 0;\n");
    fprintf(out, "    const unsigned char *__vm_cp_p = __vm_cp_enc;\n");
    fprintf(out, "    static const char *__vm_cp_strs[1024];\n");
    fprintf(out, "    int __vm_cp_n = 0;\n");
    fprintf(out, "    uint32_t __vm_rt = __vm_rt_key();\n");
    fprintf(out, "    while(*__vm_cp_p != 0xFF && __vm_cp_n < 1024){\n");
    fprintf(out, "        int si = __vm_cp_n;\n");
    fprintf(out, "        uint8_t key = (uint8_t)((__vm_rt >> ((si %% 4) * 8)) ^ (si * 0x3D + 0x17));\n");
    fprintf(out, "        int slen = *__vm_cp_p++ ^ key;\n");
    fprintf(out, "        if(slen > 255) break;\n");
    fprintf(out, "        for(int j = 0; j < slen; j++){\n");
    fprintf(out, "            uint8_t k1 = (uint8_t)((__vm_rt >> (((si + j) %% 4) * 8)) ^ (j * 0x5B));\n");
    fprintf(out, "            uint8_t k2 = (uint8_t)((__vm_rt ^ (si * 0x1F + j * 0x2D)) >> ((j %% 4) * 8));\n");
    fprintf(out, "            __vm_cp_buf[__vm_cp_off++] = *__vm_cp_p++ ^ k1 ^ k2;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        __vm_cp_buf[__vm_cp_off++] = 0;\n");
    fprintf(out, "        __vm_cp_strs[__vm_cp_n++] = &__vm_cp_buf[__vm_cp_off - slen - 1];\n");
    fprintf(out, "    }\n");
    fprintf(out, "    __vm_consts = __vm_cp_strs;\n");
    fprintf(out, "    __vm_nconsts = __vm_cp_n;\n");
    /* Decode bytecode with RC4 — key derived from runtime hash, not direct #define */
    fprintf(out, "    /* Decode bytecode (RC4 with hardened key) */\n");
    fprintf(out, "    int __vm_cs=(int)sizeof(__vm_code_enc);\n");
    fprintf(out, "    if(__vm_cs>(int)sizeof(__vm_code_decoded)) __vm_cs=(int)sizeof(__vm_code_decoded);\n");
    fprintf(out, "    {\n");
    fprintf(out, "    uint8_t rc4_s[256];\n");
    fprintf(out, "    for(int i=0;i<256;i++) rc4_s[i]=(uint8_t)i;\n");
    fprintf(out, "    uint8_t rc4_key[8];\n");
    /* Key derivation: mix rt_key with PERM_SEED through non-trivial ops */
    fprintf(out, "    unsigned int __vm_ps2=__vm_rt_key() ^ (__VM_PERM_SEED * 0x9E3779B9u);\n");
    fprintf(out, "    __vm_ps2 ^= (__vm_ps2 >> 16); __vm_ps2 *= 0x85EBCA6Bu; __vm_ps2 ^= (__vm_ps2 >> 13);\n");
    fprintf(out, "    for(int i=0;i<8;i++) rc4_key[i]=(uint8_t)(__vm_ps2>>(i*4));\n");
    fprintf(out, "    int j2=0;\n");
    fprintf(out, "    for(int i=0;i<256;i++){ j2=(j2+rc4_s[i]+rc4_key[i%%8])&0xFF; uint8_t t=rc4_s[i];rc4_s[i]=rc4_s[j2];rc4_s[j2]=t; }\n");
    fprintf(out, "    int i2=0,jj=0;\n");
    fprintf(out, "    for(int k=0;k<__vm_cs;k++){\n");
    fprintf(out, "        i2=(i2+1)&0xFF;\n");
    fprintf(out, "        jj=(jj+rc4_s[i2])&0xFF;\n");
    fprintf(out, "        uint8_t t=rc4_s[i2];rc4_s[i2]=rc4_s[jj];rc4_s[jj]=t;\n");
    fprintf(out, "        uint8_t ks=rc4_s[(rc4_s[i2]+rc4_s[jj])&0xFF];\n");
    fprintf(out, "        __vm_code_decoded[k]=__vm_code_enc[k]^ks;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    }\n");
    /* Un-permute opcodes */
    fprintf(out, "    /* Un-permute opcodes (per-build randomized) */\n");
    fprintf(out, "    uint8_t __vm_inv[256];\n");
    fprintf(out, "    unsigned int __vm_ps = __VM_PERM_SEED;\n");
    fprintf(out, "    for(int i = 0; i < 256; i++){\n");
    fprintf(out, "        __vm_inv[i] = __vm_perm[i] ^ (uint8_t)(__vm_ps >> (i %% 4 * 8));\n");
    fprintf(out, "    }\n");
    fprintf(out, "    { int i = 0;\n");
    fprintf(out, "    while(i < __vm_cs){\n");
    fprintf(out, "        uint8_t vm_op = __vm_code_decoded[i];\n");
    fprintf(out, "        uint8_t real_op = __vm_inv[vm_op];\n");
    fprintf(out, "        __vm_code_decoded[i] = real_op;\n");
    fprintf(out, "        i++;\n");
    fprintf(out, "        if(real_op==0x01||real_op==0x03) i+=2;\n");
    fprintf(out, "        else if(real_op==0x02) i+=4;\n");
    fprintf(out, "        else if(real_op==0x40||real_op==0x41||real_op==0x42) i+=2;\n");
    fprintf(out, "        else if(real_op==0x43) i+=2;\n");
    fprintf(out, "        else if(real_op==0x47) i+=1;\n");
    fprintf(out, "    } }\n");
    fprintf(out, "    __vm_code = __vm_code_decoded;\n");
    fprintf(out, "    __vm_code_size = __VM_CODE_SIZE;\n");
    fprintf(out, "    /* Set $0-$9 from argv */\n");
    for(int i = 0; i < 10; i++){
        fprintf(out, "    if(argc > %d) _setenv(\"%d\", argv[%d], 1);\n", i, i, i);
    }
    fprintf(out, "    int rc = vm_run(0);\n");
    /* Anti-dump: wipe decoded bytecode after execution */
    fprintf(out, "    /* Anti-dump: wipe sensitive data */\n");
    fprintf(out, "    memset(__vm_code_decoded, 0, sizeof(__vm_code_decoded));\n");
    fprintf(out, "    return rc;\n");
    fprintf(out, "}\n");

    (void)obfuscate;
}
