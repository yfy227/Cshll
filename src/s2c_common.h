/*
 * s2c_common.h — Common definitions, types, and utilities
 * Part of shell2c modular transpiler
 * Author: 爱摸鱼的狐狸
 */
#ifndef S2C_COMMON_H
#define S2C_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>

/* ---- Utility functions ---- */
static char *xstrdup(const char *s){ return s ? strdup(s) : strdup(""); }

static char *ltrim(char *s){ while(*s&&isspace((unsigned char)*s))s++; return s; }
static char *rtrim(char *s){
    int l=(int)strlen(s);
    while(l>0&&isspace((unsigned char)s[l-1]))s[--l]=0;
    return s;
}
static char *trim(char *s) __attribute__((unused));
static char *trim(char *s){ return rtrim(ltrim(s)); }

static int starts_with(const char *s,const char *p){
    return strncmp(s,p,strlen(p))==0;
}

/* ---- C keyword safe-naming ---- */
static const char *C_KEYWORDS[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","inline","int","long",
    "register","restrict","return","short","signed","sizeof","static","struct",
    "switch","typedef","union","unsigned","void","volatile","while",
    "and","or","not","true","false","NULL",
    NULL
};
static char _cname_buf[256];
const char *safe_cname(const char *name); /* defined in shell2c.c */

#endif /* S2C_COMMON_H */
