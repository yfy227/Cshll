/*
 * s2c_mangle.c — Name mangling implementation
 * Author: 爱摸鱼的狐狸
 */
#include "s2c_mangle.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

int g_obfuscate = 0;

/* Simple deterministic hash (FNV-1a variant) */
static unsigned int fnv_hash(const char *s){
    unsigned int h = 2166136261u;
    while(*s){
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

void mangle_init(int obfuscate_mode){
    g_obfuscate = obfuscate_mode;
}

/* Mangle a single name */
const char *mangle(const char *name){
    if(!g_obfuscate) return name;
    if(!name || !*name) return name;

    /* Don't mangle C standard library names */
    if(name[0] != '_') return name;

    /* Don't mangle _Q* (already mangled) */
    if(name[0]=='_' && name[1]=='Q') return name;

    /* Don't mangle C keywords and compiler attributes */
    if(strcmp(name,"__attribute__")==0) return name;
    if(strcmp(name,"__unused__")==0) return name;
    if(strcmp(name,"unused")==0) return name;
    if(strcmp(name,"static")==0) return name;
    if(strcmp(name,"const")==0) return name;
    if(strcmp(name,"void")==0) return name;
    if(strcmp(name,"int")==0) return name;
    if(strcmp(name,"char")==0) return name;
    if(strcmp(name,"long")==0) return name;
    if(strcmp(name,"unsigned")==0) return name;
    if(strcmp(name,"volatile")==0) return name;
    if(strcmp(name,"sizeof")==0) return name;
    if(strcmp(name,"return")==0) return name;
    if(strcmp(name,"struct")==0) return name;
    if(strcmp(name,"typedef")==0) return name;
    if(strcmp(name,"enum")==0) return name;
    if(strcmp(name,"NULL")==0) return name;

    /* Don't mangle _enc_, _end_, _xd, _dt, _di, _reg, _cal, _opq, _dec, _s, _v */
    if(strncmp(name,"_enc_",5)==0) return name;
    if(strncmp(name,"_end",4)==0) return name;
    if(strcmp(name,"_xd")==0) return name;
    if(strcmp(name,"_dt")==0) return name;
    if(strcmp(name,"_di")==0) return name;
    if(strcmp(name,"_reg")==0) return name;
    if(strcmp(name,"_cal")==0) return name;
    if(strcmp(name,"_opq")==0) return name;
    if(strcmp(name,"_dec")==0) return name;

    static char buf[256];
    unsigned int h = fnv_hash(name);

    /* Generate opaque name: _Q + hex hash */
    snprintf(buf, sizeof(buf), "_Q%08x", h);
    return buf;
}

/* Mangle __sh_* names */
const char *mangle_sh(const char *name){
    if(!g_obfuscate) return name;
    /* name starts with __sh_ */
    return mangle(name);
}

/* Mangle __b_* names */
const char *mangle_b(const char *name){
    if(!g_obfuscate) return name;
    return mangle(name);
}

/* Mangle __tw_* names */
const char *mangle_tmp(const char *name){
    if(!g_obfuscate) return name;
    return mangle(name);
}

/* Mangle references inside string literals.
 * This handles cases where __sh_* names appear inside RT_HEADER strings. */
const char *mangle_string(const char *s){
    if(!g_obfuscate) return s;
    if(!s || !*s) return s;

    /* Check if string contains __sh_ or __b_ references */
    if(!strstr(s, "__sh_") && !strstr(s, "__b_") &&
       !strstr(s, "__tw_") && !strstr(s, "__fl") &&
       !strstr(s, "__fa") && !strstr(s, "__sfd_") &&
       !strstr(s, "__pfd") && !strstr(s, "__exit_status") &&
       !strstr(s, "__sh_arg"))
        return s;

    /* For RT_HEADER strings, we need to do string replacement.
     * This is complex, so we return a modified copy. */
    static char result[65536];
    char *dst = result;
    const char *src = s;
    int modified = 0;

    while(*src && (dst - result) < (int)sizeof(result) - 32){
        if(strncmp(src, "__sh_", 5)==0 || strncmp(src, "__b_", 4)==0){
            /* Find end of identifier */
            int prefix_len = (src[2]=='s') ? 5 : 4;
            const char *start = src;
            src += prefix_len;
            while(*src && (isalnum((unsigned char)*src) || *src=='_')) src++;
            /* Hash the full name */
            char namebuf[128];
            int len = src - start;
            if(len < 128){
                memcpy(namebuf, start, len);
                namebuf[len] = 0;
                const char *mangled = mangle(namebuf);
                int mlen = strlen(mangled);
                if(dst + mlen < result + sizeof(result) - 1){
                    memcpy(dst, mangled, mlen);
                    dst += mlen;
                    modified = 1;
                    continue;
                }
            }
            /* Fallback: copy original */
            src = start;
        }
        if(strncmp(src, "__exit_status", 13)==0){
            const char *mangled = mangle("__exit_status");
            int mlen = strlen(mangled);
            if(dst + mlen < result + sizeof(result) - 1){
                memcpy(dst, mangled, mlen);
                dst += mlen;
                src += 13;
                modified = 1;
                continue;
            }
        }
        if(strncmp(src, "__tw_", 5)==0){
            /* __tw_N — temp word buffer */
            const char *start = src;
            src += 5;
            while(*src && isdigit((unsigned char)*src)) src++;
            char namebuf[128];
            int len = src - start;
            if(len < 128){
                memcpy(namebuf, start, len);
                namebuf[len] = 0;
                const char *mangled = mangle(namebuf);
                int mlen = strlen(mangled);
                if(dst + mlen < result + sizeof(result) - 1){
                    memcpy(dst, mangled, mlen);
                    dst += mlen;
                    modified = 1;
                    continue;
                }
            }
            src = start;
        }
        if(strncmp(src, "__fl", 4)==0 || strncmp(src, "__fa", 4)==0 ||
           strncmp(src, "__fi", 4)==0 || strncmp(src, "__ai", 4)==0 ||
           strncmp(src, "__an", 4)==0 || strncmp(src, "__cl", 4)==0 ||
           strncmp(src, "__rb", 3)==0 || strncmp(src, "__sp", 3)==0 ||
           strncmp(src, "__sv", 3)==0 || strncmp(src, "__se", 3)==0 ||
           strncmp(src, "__sc", 3)==0 || strncmp(src, "__hf", 3)==0 ||
           strncmp(src, "__hfd", 4)==0 || strncmp(src, "__cmd", 5)==0 ||
           strncmp(src, "__psi", 5)==0 || strncmp(src, "__pso", 5)==0 ||
           strncmp(src, "__ppid", 6)==0 || strncmp(src, "__pfd", 4)==0 ||
           strncmp(src, "__sfd_", 5)==0 || strncmp(src, "__bg", 4)==0 ||
           strncmp(src, "__tw_", 5)==0 || strncmp(src, "__w1", 4)==0 ||
           strncmp(src, "__w2", 4)==0 || strncmp(src, "__vi", 4)==0 ||
           strncmp(src, "__tok", 5)==0 || strncmp(src, "__rl", 3)==0 ||
           strncmp(src, "__pf", 3)==0 || strncmp(src, "__al", 3)==0 ||
           strncmp(src, "__ai", 3)==0 || strncmp(src, "__hdb", 4)==0 ||
           strncmp(src, "__tw_", 5)==0){
            /* Generic __xx* identifier — mangle the whole identifier */
            const char *start = src;
            src++; /* skip first _ */
            while(*src && (isalnum((unsigned char)*src) || *src=='_')) src++;
            char namebuf[128];
            int len = src - start;
            if(len < 128){
                memcpy(namebuf, start, len);
                namebuf[len] = 0;
                const char *mangled = mangle(namebuf);
                int mlen = strlen(mangled);
                if(dst + mlen < result + sizeof(result) - 1){
                    memcpy(dst, mangled, mlen);
                    dst += mlen;
                    modified = 1;
                    continue;
                }
            }
            src = start;
        }
        *dst++ = *src++;
    }
    *dst = 0;

    if(modified) return result;
    return s;
}
