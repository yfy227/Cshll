/* Auto-generated from runtime.inc */
#ifndef S2C_RUNTIME_LIVE_H
#define S2C_RUNTIME_LIVE_H

/* ---- shell2c runtime (deep optimization) ---- */
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
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <regex.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <glob.h>
#include <fnmatch.h>
#include <stdarg.h>

static int __exit_status=0;
static int __sh_argc=0;
static char **__sh_args=NULL;
static int __sh_last_bg_pid=0;
static int __sh_set_e=0;
static int __sh_set_u=0;
static int __sh_set_x=0;
/* Bash special variables */
static int __sh_lineno=0;
static int __sh_pipestatus[16]={0};
static int __sh_shlvl=1;
static unsigned int __sh_rand_seed=0;
static time_t __sh_start_time=0;
/* __sh_getenv_special: resolve bash special vars (uses static buffer, no pool dependency) */
static const char *__sh_special_var(const char *name){
  static char __sv_buf[256];
  if(!strcmp(name,"RANDOM")){
    if(!__sh_rand_seed) __sh_rand_seed=(unsigned int)time(NULL)^(unsigned int)getpid();
    __sh_rand_seed=__sh_rand_seed*1103515245+12345;
    snprintf(__sv_buf,sizeof(__sv_buf),"%d",(__sh_rand_seed/65536)%32768); return __sv_buf;
  }
  if(!strcmp(name,"SECONDS")){
    snprintf(__sv_buf,sizeof(__sv_buf),"%ld",(long)(time(NULL)-__sh_start_time)); return __sv_buf;
  }
  if(!strcmp(name,"LINENO")){
    snprintf(__sv_buf,sizeof(__sv_buf),"%d",__sh_lineno); return __sv_buf;
  }
  if(!strcmp(name,"BASHPID")){
    snprintf(__sv_buf,sizeof(__sv_buf),"%d",(int)getpid()); return __sv_buf;
  }
  if(!strcmp(name,"SHLVL")){
    snprintf(__sv_buf,sizeof(__sv_buf),"%d",__sh_shlvl); return __sv_buf;
  }
  if(!strcmp(name,"PIPESTATUS")){ return "0"; }
  if(!strcmp(name,"HOSTTYPE")){
#if defined(__x86_64__) || defined(__amd64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm";
#else
    return "unknown";
#endif
  }
  if(!strcmp(name,"OSTYPE")){
#if defined(__linux__)
    return "linux-gnu";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "unknown";
#endif
  }
  if(!strcmp(name,"MACHTYPE")){ return __sh_special_var("HOSTTYPE"); }
  return NULL;
}

/* ---- rotating buffer pools (safe for nested calls) ---- */
/* 4-slot pools: calling a function 4+ times in one expression still
 * overwrites, but that's rare. 4 is the sweet spot for memory vs safety. */
static char __sh_indirect_p0[1024], __sh_indirect_p1[1024], __sh_indirect_p2[1024], __sh_indirect_p3[1024];
static int  __sh_indirect_i = 0;
static char __sh_substr_p0[4096], __sh_substr_p1[4096], __sh_substr_p2[4096], __sh_substr_p3[4096];
static int  __sh_substr_i = 0;
static char __sh_strip_p0[4096], __sh_strip_p1[4096], __sh_strip_p2[4096], __sh_strip_p3[4096];
static int  __sh_strip_i = 0;
static char __sh_replace_p0[4096], __sh_replace_p1[4096], __sh_replace_p2[4096], __sh_replace_p3[4096];
static int  __sh_replace_i = 0;
static char __sh_upper_p0[4096], __sh_upper_p1[4096], __sh_upper_p2[4096], __sh_upper_p3[4096];
static int  __sh_upper_i = 0;
static char __sh_lower_p0[4096], __sh_lower_p1[4096], __sh_lower_p2[4096], __sh_lower_p3[4096];
static int  __sh_lower_i = 0;
static char __sh_cmd_out_p0[65536], __sh_cmd_out_p1[65536], __sh_cmd_out_p2[65536], __sh_cmd_out_p3[65536];
static int  __sh_cmd_out_i = 0;
static char __sh_fmt_p0[8192], __sh_fmt_p1[8192], __sh_fmt_p2[8192], __sh_fmt_p3[8192];
static int  __sh_fmt_i = 0;
static char __sh_fn_out_p0[65536], __sh_fn_out_p1[65536], __sh_fn_out_p2[65536], __sh_fn_out_p3[65536];
static int  __sh_fn_out_i = 0;
static char __sh_arr_p0[8192], __sh_arr_p1[8192], __sh_arr_p2[8192], __sh_arr_p3[8192];
static int  __sh_arr_i = 0;
static char __sh_ps_p0[64], __sh_ps_p1[64], __sh_ps_p2[64], __sh_ps_p3[64];
static int  __sh_ps_i = 0;

/* Helper: get next pool slot for a given base name */
static char *__sh_pool_next(char *p0, char *p1, char *p2, char *p3, int *idx){
  char *r = ( (*idx == 0) ? p0 : (*idx == 1) ? p1 : (*idx == 2) ? p2 : p3 );
  *idx = (*idx + 1) & 3;
  return r;
}

/* ---- string helpers ---- */
static const char *__sh_indirect(const char *name){
  char *buf = __sh_pool_next(__sh_indirect_p0,__sh_indirect_p1,__sh_indirect_p2,__sh_indirect_p3,&__sh_indirect_i);
  char vn[256]; snprintf(vn,sizeof(vn),"%s",name);
  const char *v=getenv(vn); if(v) { snprintf(buf,1024,"%s",v); return buf; }
  buf[0]=0; return buf;
}
static const char *__sh_getenv(const char *name){
  const char *v=getenv(name); return v?v:"";
}
static const char *__sh_arr_join(const char **arr){
  char *buf = __sh_pool_next(__sh_arr_p0,__sh_arr_p1,__sh_arr_p2,__sh_arr_p3,&__sh_arr_i);
  buf[0]=0;
  for(int i=0;arr[i];i++){ if(i>0) strncat(buf," ",8192-strlen(buf)-1); strncat(buf,arr[i]?arr[i]:"",8192-strlen(buf)-1); }
  return buf;
}
/* Join function args (for $@ / $*) */
static const char *__sh_join_args(int argc, char **args){
  char *buf = __sh_pool_next(__sh_arr_p0,__sh_arr_p1,__sh_arr_p2,__sh_arr_p3,&__sh_arr_i);
  buf[0]=0;
  for(int i=0;i<argc;i++){ if(i>0) strncat(buf," ",8192-strlen(buf)-1); strncat(buf,args[i]?args[i]:"",8192-strlen(buf)-1); }
  return buf;
}
/* ${!#} — return last positional arg */
static const char *__sh_indirect_args(int argc, char **args){
  if(argc>=1 && args) return args[argc-1]?args[argc-1]:"";
  return "";
}
static int __sh_safe_div_guard(void){ fprintf(stderr,"division by 0\n"); __exit_status=1; return 1; }
static int __sh_safe_div(int a,int b){ if(b==0){ fprintf(stderr,"division by 0\n"); __exit_status=1; return 0; } return a/b; }
static int __sh_safe_mod(int a,int b){ if(b==0){ fprintf(stderr,"modulo by 0\n"); __exit_status=1; return 0; } return a%b; }
static int __sh_arr_count(const char **arr){
  int n=0; for(int i=0;i<256;i++) if(arr[i]) n++; return n;
}
/* BUG-10 fix: associative array helpers — store as key,value,key,value,... */
static const char *__sh_assoc_get(const char **arr,const char *key){
  for(int i=0;arr[i]&&arr[i+1];i+=2){ if(strcmp(arr[i],key)==0) return arr[i+1]; }
  return "";
}
static void __sh_assoc_set(const char **arr,const char *key,const char *val){
  for(int i=0;i<254;i+=2){
    if(!arr[i]){ arr[i]=strdup(key); arr[i+1]=strdup(val); return; }
    if(arr[i]&&strcmp(arr[i],key)==0){ free((void*)arr[i+1]); arr[i+1]=strdup(val); return; }
  }
}
static long __sh_pow(long base,long exp){
  if(exp<0) return 0;
  long r=1; while(exp-->0) r*=base; return r;
}
static int __sh_div_zero(const char *expr){
  (void)expr;
  fprintf(stderr,"shell2c: division by zero\n");
  __exit_status=1;
  return 0;
}
static const char *__sh_arr_slice(const char **arr,int off,int len){
  char *buf = __sh_pool_next(__sh_arr_p0,__sh_arr_p1,__sh_arr_p2,__sh_arr_p3,&__sh_arr_i);
  int n=0; while(arr[n]) n++;
  if(off<0) off+=n;
  if(off<0) off=0;
  if(off>n) off=n;
  int end=(len<0)?n:off+len; if(end>n) end=n;
  buf[0]=0;
  for(int i=off;i<end;i++){ if(i>off) strncat(buf," ",8192-strlen(buf)-1); strncat(buf,arr[i]?arr[i]:"",8192-strlen(buf)-1); }
  return buf;
}
static const char *__sh_substr(const char *s,int off,int len){
  char *buf = __sh_pool_next(__sh_substr_p0,__sh_substr_p1,__sh_substr_p2,__sh_substr_p3,&__sh_substr_i);
  int n=(int)strlen(s);
  if(off<0) off+=n;
  if(off<0) off=0;
  if(off>n) off=n;
  int end = (len<0)? n : off+len; if(end>n) end=n;
  int l=end-off; if(l<0) l=0; if(l>=4096) l=4095;
  memcpy(buf,s+off,l); buf[l]=0;
  return buf;
}
static const char *__sh_strip_prefix(const char *s,const char *pat,int greedy){
  char *buf = __sh_pool_next(__sh_strip_p0,__sh_strip_p1,__sh_strip_p2,__sh_strip_p3,&__sh_strip_i);
  int best=0; int n=(int)strlen(s);
  if(greedy){ for(int i=n;i>=1;i--){ char tmp[1024]; int l=i; if(l>=(int)sizeof(tmp)) continue; memcpy(tmp,s,l); tmp[l]=0; if(fnmatch(pat,tmp,0)==0){ best=i; break; } } }
  else { for(int i=1;i<=n;i++){ char tmp[1024]; int l=i; if(l>=(int)sizeof(tmp)) continue; memcpy(tmp,s,l); tmp[l]=0; if(fnmatch(pat,tmp,0)==0){ best=i; break; } } }
  snprintf(buf,4096,"%s",s+best);
  return buf;
}
static const char *__sh_strip_suffix(const char *s,const char *pat,int greedy){
  char *buf = __sh_pool_next(__sh_strip_p0,__sh_strip_p1,__sh_strip_p2,__sh_strip_p3,&__sh_strip_i);
  int n=(int)strlen(s); int best=n;
  if(!greedy){ for(int i=n;i>=1;i--){ if(fnmatch(pat,s+i,0)==0){ best=i; break; } } }
  else { for(int i=1;i<=n;i++){ if(fnmatch(pat,s+i,0)==0){ best=i; break; } } }
  int l=best; if(l<0) l=0; if(l>=4096) l=4095;
  memcpy(buf,s,l); buf[l]=0;
  return buf;
}
static const char *__sh_replace(const char *s,const char *old,const char *newp,int global,int anchor_start,int anchor_end){
  char *out = __sh_pool_next(__sh_replace_p0,__sh_replace_p1,__sh_replace_p2,__sh_replace_p3,&__sh_replace_i);
  int oi=0; int sl=(int)strlen(s); int ol=(int)strlen(old); int nl=(int)strlen(newp);
  int i=0; int done=0;
  while(i<sl && oi<4096-nl-2){
    if(!done && (anchor_start?i==0:1) && strncmp(s+i,old,ol)==0){
      memcpy(out+oi,newp,nl); oi+=nl; i+=ol; if(!global){ done=1; }
      if(anchor_start) anchor_start=0;
    } else { out[oi++]=s[i++]; }
  }
  out[oi]=0;
  return out;
}
static const char *__sh_upper(const char *s){
  char *buf = __sh_pool_next(__sh_upper_p0,__sh_upper_p1,__sh_upper_p2,__sh_upper_p3,&__sh_upper_i);
  int i; for(i=0;s[i]&&i<4095;i++) buf[i]=toupper((unsigned char)s[i]); buf[i]=0; return buf;
}
static const char *__sh_lower(const char *s){
  char *buf = __sh_pool_next(__sh_lower_p0,__sh_lower_p1,__sh_lower_p2,__sh_lower_p3,&__sh_lower_i);
  int i; for(i=0;s[i]&&i<4095;i++) buf[i]=tolower((unsigned char)s[i]); buf[i]=0; return buf;
}
static const char *__sh_cmd_output(const char *cmd){
  char *buf = __sh_pool_next(__sh_cmd_out_p0,__sh_cmd_out_p1,__sh_cmd_out_p2,__sh_cmd_out_p3,&__sh_cmd_out_i);
  /* Use bash -c for brace expansion support ({1..10}, {a,b,c} etc.) */
  char fullcmd[65536]; snprintf(fullcmd,sizeof(fullcmd),"bash -c '%s'",cmd);
  FILE *p=popen(fullcmd,"r"); if(!p){
    /* fallback to /bin/sh if bash not available */
    p=popen(cmd,"r"); if(!p){ buf[0]=0; return buf; }
  }
  size_t n=fread(buf,1,65535,p); pclose(p);
  if(n>0 && buf[n-1]=='\n') n--;
  buf[n]=0;
  return buf;
}
static void __sh_usleep(unsigned us){ usleep(us); }
static const char *__sh_fmt(const char *fmt, ...){
  char *buf = __sh_pool_next(__sh_fmt_p0,__sh_fmt_p1,__sh_fmt_p2,__sh_fmt_p3,&__sh_fmt_i);
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, 8192, fmt, ap);
  va_end(ap); return buf;
}
static const char *__sh_capture_fn(void (*fn)(int,char**), int argc, char **argv){
  char *buf = __sh_pool_next(__sh_fn_out_p0,__sh_fn_out_p1,__sh_fn_out_p2,__sh_fn_out_p3,&__sh_fn_out_i);
  int pfd[2]; if(pipe(pfd)<0){ buf[0]=0; return buf; }
  pid_t pid=fork();
  if(pid==0){
    close(pfd[0]); dup2(pfd[1],1); close(pfd[1]);
    setvbuf(stdout,NULL,_IONBF,0);
    fn(argc,argv);
    fflush(stdout); _exit(0);
  }
  close(pfd[1]);
  FILE *fp=fdopen(pfd[0],"r"); if(!fp){ close(pfd[0]); waitpid(pid,NULL,0); buf[0]=0; return buf; }
  size_t n=fread(buf,1,65535,fp);
  fclose(fp); /* also closes underlying pfd[0] */
  waitpid(pid,NULL,0);
  if(n>0 && buf[n-1]=='\n') n--;
  buf[n]=0;
  return buf;
}
/* Process substitution: <(cmd) or >(cmd).
 * Returns a /dev/fd/N path string. For <(cmd), cmd's stdout is connected
 * to the read end. For >(cmd), cmd's stdin is connected to the write end. */
static const char *__sh_proc_subst(const char *cmd, char dir){
  char *buf = __sh_pool_next(__sh_ps_p0,__sh_ps_p1,__sh_ps_p2,__sh_ps_p3,&__sh_ps_i);
  int pfd[2]; if(pipe(pfd)<0){ snprintf(buf,64,"/dev/null"); return buf; }
  pid_t pid=fork();
  if(pid==0){
    if(dir=='<'){ close(pfd[0]); dup2(pfd[1],1); close(pfd[1]); }
    else { close(pfd[1]); dup2(pfd[0],0); close(pfd[0]); }
    execl("/bin/sh","sh","-c",cmd,(char*)NULL); _exit(127);
  }
  if(dir=='<'){ close(pfd[1]); snprintf(buf,64,"/dev/fd/%d",pfd[0]); }
  else { close(pfd[0]); snprintf(buf,64,"/dev/fd/%d",pfd[1]); }
  return buf;
}
static void __sh_echo_escape(const char *s){
  while(*s){ if(*s=='\\' && *(s+1)){ s++; switch(*s){ case 'n':putchar('\n');break; case 't':putchar('\t');break; case 'r':putchar('\r');break; case '\\':putchar('\\');break; case '0':putchar('\0');break; default:putchar(*s);break; } s++; } else putchar(*s++); }
}
static void __sh_printf(const char *fmt,...){
  /* Shell-style printf: all args are strings, %d/%i convert via atoi */
  /* Supports format modifiers: %05d, %-10s, %.2f, etc. */
  va_list ap; va_start(ap,fmt);
  const char *p=fmt;
  while(*p){
    if(*p=='%' && *(p+1)){
      p++;
      /* collect format modifiers: flags, width, .precision */
      char mods[32]; int mi=0;
      while(*p && (*p=='-'||*p=='+'||*p==' '||*p=='#'||*p=='0'||
             (*p>='1'&&*p<='9')||*p=='.'||*p=='*') && mi<30){
        mods[mi++]=*p++;
      }
      mods[mi]=0;
      if(*p=='d'||*p=='i'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sd",mods); printf(f,s?atoi(s):0); }
      else if(*p=='s'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%ss",mods); printf(f,s?s:""); }
      else if(*p=='c'){ const char *s=va_arg(ap,const char*); putchar(s?s[0]:' '); }
      else if(*p=='x'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sx",mods); printf(f,s?atoi(s):0); }
      else if(*p=='X'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sX",mods); printf(f,s?atoi(s):0); }
      else if(*p=='o'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%so",mods); printf(f,s?atoi(s):0); }
      else if(*p=='f'||*p=='e'||*p=='g'||*p=='E'||*p=='G'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sf",mods); printf(f,s?atof(s):0.0); }
      else if(*p=='%'){ putchar('%'); }
      else { putchar('%'); fputs(mods,stdout); putchar(*p); }
    } else { putchar(*p); }
    p++;
  }
  va_end(ap); fflush(stdout);
}
/* BUG-16 fix: printf -v variant — write to buffer instead of stdout */
static void __sh_sprintf(char *buf, int bufsz, const char *fmt,...){
  va_list ap; va_start(ap,fmt);
  int bi=0;
  const char *p=fmt;
  while(*p && bi<bufsz-1){
    if(*p=='%' && *(p+1)){
      p++;
      char mods[32]; int mi=0;
      while(*p && (*p=='-'||*p=='+'||*p==' '||*p=='#'||*p=='0'||
             (*p>='1'&&*p<='9')||*p=='.'||*p=='*') && mi<30){
        mods[mi++]=*p++;
      }
      mods[mi]=0;
      if(*p=='d'||*p=='i'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sd",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?atoi(s):0); }
      else if(*p=='s'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%ss",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?s:""); }
      else if(*p=='c'){ const char *s=va_arg(ap,const char*); buf[bi++]=s?s[0]:' '; }
      else if(*p=='x'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sx",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?atoi(s):0); }
      else if(*p=='X'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sX",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?atoi(s):0); }
      else if(*p=='o'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%so",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?atoi(s):0); }
      else if(*p=='f'||*p=='e'||*p=='g'){ const char *s=va_arg(ap,const char*); char f[40]; snprintf(f,sizeof(f),"%%%sf",mods); bi+=snprintf(buf+bi,bufsz-bi,f,s?atof(s):0.0); }
      else if(*p=='%'){ buf[bi++]='%'; }
      else { buf[bi++]='%'; for(int j=0;j<mi;j++) buf[bi++]=mods[j]; buf[bi++]=*p; }
    } else { buf[bi++]=*p; }
    p++;
  }
  buf[bi]=0; va_end(ap);
}
/* Elegant output helpers — reduce repetitive fputs+putchar patterns */
static void __sh_puts(const char *s){ fputs(s,stdout); putchar('\n'); }
static void __sh_putf(const char *fmt,...){
  va_list ap; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap); putchar('\n'); fflush(stdout);
}
static void __sh_arr_free(char **arr){ for(int i=0;arr[i];i++) free(arr[i]); free(arr); }
/* ---- test(1) helpers ---- */
static int __sh_test_file(const char *p,int dir){ struct stat st; if(stat(p,&st)!=0) return 0; return dir?S_ISDIR(st.st_mode):1; }
static int __sh_test_sfile(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_size>0; }
static int __sh_test_link(const char *p){ struct stat st; if(lstat(p,&st)!=0) return 0; return S_ISLNK(st.st_mode); }
static int __sh_test_fifo(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISFIFO(st.st_mode); }
static int __sh_test_sock(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISSOCK(st.st_mode); }
static int __sh_test_blk(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISBLK(st.st_mode); }
static int __sh_test_chr(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return S_ISCHR(st.st_mode); }
static int __sh_test_mode(const char *p,int mask){ struct stat st; if(stat(p,&st)!=0) return 0; return (st.st_mode&mask)!=0; }
static int __sh_test_var(const char *name){ return getenv(name)!=NULL; }
static int __sh_test_owner(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_uid==getuid(); }
static int __sh_test_group(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_gid==getgid(); }
static int __sh_test_newer(const char *p){ struct stat st; if(stat(p,&st)!=0) return 0; return st.st_mtime>st.st_atime; }
static int __sh_test_same(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0||stat(b,&sb)!=0) return 0; return sa.st_dev==sb.st_dev&&sa.st_ino==sb.st_ino; }
static int __sh_test_nt(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0) return 0; if(stat(b,&sb)!=0) return 1; return sa.st_mtime>sb.st_mtime; }
static int __sh_test_ot(const char *a,const char *b){ struct stat sa,sb; if(stat(a,&sa)!=0) return 0; if(stat(b,&sb)!=0) return 0; return sa.st_mtime<sb.st_mtime; }
static int __sh_test_cmd(const char *cmd){ FILE *p=popen(cmd,"r"); if(!p) return 0; char buf[1024]; int got=0; while(fgets(buf,sizeof(buf),p)){ got=1; } pclose(p); return got; }
static int __sh_regex(const char *s,const char *pat){ regex_t r; if(regcomp(&r,pat,REG_EXTENDED|REG_NOSUB)!=0) return 0; int rc=regexec(&r,s,0,NULL,0); regfree(&r); return rc==0; }

/* ---- file builtins ---- */
static void __attribute__((unused)) __b_pwd(void){ char b[4096]; if(getcwd(b,sizeof(b)))printf("%s\n",b); else perror("pwd"); }
static void __attribute__((unused)) __b_ls(const char *flags,...){
  va_list ap; va_start(ap,flags);
  int show_all=0,long_fmt=0,rev=0;
  for(const char *f=flags;f&&*f;f++){ if(*f=='a')show_all=1; if(*f=='l')long_fmt=1; if(*f=='r')rev=1; }
  const char *path; int first=1; int had_arg=0;
  while((path=va_arg(ap,const char*))!=NULL){
    had_arg=1;
    DIR *dp=opendir(path[0]?path:"."); if(!dp){char __lbuf[512]; int __lbn=snprintf(__lbuf,sizeof(__lbuf),"ls: cannot access '%s': %s\n",path,strerror(errno)); write(2,__lbuf,__lbn); __exit_status=2;continue;}
    if(!first) printf("\n");
    first=0;
    struct dirent *e; struct stat st; char full[4096];
    while((e=readdir(dp))){ if(!show_all&&e->d_name[0]=='.') continue;
      if(long_fmt){ snprintf(full,sizeof(full),"%s/%s",path,e->d_name); if(stat(full,&st)==0){ printf("%c %8ld %s\n",S_ISDIR(st.st_mode)?'d':'-',(long)st.st_size,e->d_name); } else printf("? %8s %s\n","?",e->d_name); }
      else printf("%s\n",e->d_name);
    }
    closedir(dp);
  }
  if(!had_arg){ DIR *dp=opendir("."); if(dp){ struct dirent *e; while((e=readdir(dp)))if(show_all||e->d_name[0]!='.')printf("%s\n",e->d_name); closedir(dp);} }
  va_end(ap);
}
static int __attribute__((unused)) __b_cp(const char *s,const char *d){
  struct stat st; if(stat(s,&st)==0 && S_ISDIR(st.st_mode)){ char cmd[8192]; snprintf(cmd,sizeof(cmd),"cp -r \"%s\" \"%s\"",s,d); return system(cmd); }
  int f1=open(s,O_RDONLY); if(f1<0){perror("cp");return-1;}
  int f2=open(d,O_WRONLY|O_CREAT|O_TRUNC,0644); if(f2<0){perror("cp");close(f1);return-1;}
  char buf[65536]; ssize_t r;
  while((r=read(f1,buf,sizeof(buf)))>0){ ssize_t w=0;while(w<r){ssize_t x=write(f2,buf+w,r-w);if(x<0){perror("cp");close(f1);close(f2);return-1;}w+=x;}}
  close(f1);close(f2);return 0;
}
static int __attribute__((unused)) __b_mv(const char *s,const char *d){if(rename(s,d)!=0){perror("mv");return-1;}return 0;}
static int __attribute__((unused)) __b_rm(const char *p,int recursive){
  struct stat st; if(stat(p,&st)!=0){perror("rm");return-1;}
  if(S_ISDIR(st.st_mode)){ if(recursive){ char cmd[8192]; snprintf(cmd,sizeof(cmd),"rm -rf \"%s\"",p); return system(cmd); } else { if(rmdir(p)!=0){perror("rm");return-1;} } }
  else { if(unlink(p)!=0){perror("rm");return-1;} }
  return 0;
}
static int __attribute__((unused)) __b_mkdir(const char *p,int parents){
  if(parents){ char tmp[1024]; strncpy(tmp,p,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0; int l=(int)strlen(tmp);
    for(int i=1;i<l;i++){ if(tmp[i]=='/'){ tmp[i]=0; mkdir(tmp,0755); tmp[i]='/'; } }
    return mkdir(tmp,0755);
  }
  if(mkdir(p,0755)!=0){perror("mkdir");return-1;} return 0;
}
static int __attribute__((unused)) __b_rmdir(const char *p){if(rmdir(p)!=0){perror("rmdir");return-1;}return 0;}
static void __attribute__((unused)) __b_touch(const char *p){ int fd=open(p,O_WRONLY|O_CREAT,0644);if(fd>=0)close(fd);else perror("touch");}
static int __attribute__((unused)) __b_ln(const char *a,const char *b,int sym){ if(sym){ if(symlink(a,b)!=0){perror("ln");return-1;} } else { if(link(a,b)!=0){perror("ln");return-1;} } return 0; }
static int __attribute__((unused)) __b_chmod(const char *mode,const char *p){ int m=(int)strtol(mode,NULL,8); if(chmod(p,m)!=0){perror("chmod");return-1;} return 0; }
static int __attribute__((unused)) __b_chown(const char *owner,const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),"chown %s \"%s\"",owner,p); return system(cmd); }
static void __attribute__((unused)) __b_stat(const char *p){ struct stat st; if(stat(p,&st)!=0){perror("stat");return;} printf("  File: %s\n  Size: %ld\n",p,(long)st.st_size); printf("  Mode: %o\n",st.st_mode&0777); printf("  Uid: %d  Gid: %d\n",st.st_uid,st.st_gid); }
static void __attribute__((unused)) __b_du(const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),"du -sh \"%s\" 2>/dev/null",p); system(cmd); }
static void __attribute__((unused)) __b_df(void){ system("df -h"); }
static void __attribute__((unused)) __b_file(const char *p){ char cmd[1024]; snprintf(cmd,sizeof(cmd),"file \"%s\"",p); system(cmd); }
static void __attribute__((unused)) __b_basename(const char *p){ const char *b=strrchr(p,'/'); printf("%s\n",b?b+1:p); }
static void __attribute__((unused)) __b_dirname(const char *p){ char tmp[1024]; strncpy(tmp,p,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0; char *sl=strrchr(tmp,'/'); if(sl){ *sl=0; printf("%s\n",tmp[0]?tmp:"/"); } else printf(".\n"); }
static void __attribute__((unused)) __b_realpath(const char *p){ char *r=realpath(p,NULL); if(r){ printf("%s\n",r); free(r); } else perror("realpath"); }
static void __attribute__((unused)) __b_readlink(const char *p){ char buf[4096]; ssize_t n=readlink(p,buf,sizeof(buf)-1); if(n<0) perror("readlink"); else { buf[n]=0; printf("%s\n",buf); } }
static void __attribute__((unused)) __b_mktemp(void){ char t[]="/tmp/sh2cXXXXXX"; int fd=mkstemp(t); if(fd>=0){ printf("%s\n",t); close(fd); } }
static int __attribute__((unused)) __b_install(const char *s,const char *d){ return __b_cp(s,d); }

/* ---- text builtins ---- */
static void __attribute__((unused)) __b_cat(const char *path,int flags){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("cat");return;}
  char ln[8192]; int n=1;
  while(fgets(ln,sizeof(ln),f)){ if(flags&1) printf("%6d  %s",n,ln); else fputs(ln,stdout); if(flags&2){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\n') ln[l-1]='$'; } n++; }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_grep(const char *pat,const char *path,const char *flags){
  int ignore_case=(flags&&strchr(flags,'i'))?REG_ICASE:0;
  int invert=(flags&&strchr(flags,'v'))?1:0;
  int line_no=(flags&&strchr(flags,'n'))?1:0;
  int count_only=(flags&&strchr(flags,'c'))?1:0;
  int match_count=0;
  regex_t r; if(regcomp(&r,pat,REG_EXTENDED|ignore_case)!=0){ fprintf(stderr,"grep: bad pattern\n"); return; }
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("grep");regfree(&r);return;}
  char ln[8192]; int n=1;
  while(fgets(ln,sizeof(ln),f)){ int m=regexec(&r,ln,0,NULL,0)==0; if(m!=invert){ if(count_only) match_count++; else { if(line_no) printf("%d:",n); fputs(ln,stdout); } } n++; }
  if(path)fclose(f);
  if(count_only) printf("%d\n",match_count);
  regfree(&r);
}
static void __attribute__((unused)) __b_sed(const char *script,const char *path){
  /* support s/old/new/[g] and s|old|new|[g] */
  if(script[0]!='s'){ if(path){ char cmd[8192]; snprintf(cmd,sizeof(cmd),"sed '%s' \"%s\"",script,path); system(cmd); } else { char cmd[8192]; snprintf(cmd,sizeof(cmd),"sed '%s'",script); system(cmd); } return; }
  char delim=script[1]; const char *p=script+2; char old[1024]; int oi=0;
  while(*p && *p!=delim && oi<(int)sizeof(old)-1) old[oi++]=*p++;
  old[oi]=0; if(*p) p++;
  char newp[1024]; int ni=0;
  while(*p && *p!=delim && ni<(int)sizeof(newp)-1) newp[ni++]=*p++;
  newp[ni]=0; if(*p) p++;
  int global=0; while(*p){ if(*p=='g') global=1; p++; }
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("sed");return;}
  char ln[8192];
  while(fgets(ln,sizeof(ln),f)){ char out[16384]; int oi2=0; int i=0; int ol=(int)strlen(old); int nl=(int)strlen(newp); int sl=(int)strlen(ln); int done=0;
    while(i<sl && oi2<(int)sizeof(out)-nl-2){ if(!done && ol>0 && strncmp(ln+i,old,ol)==0){ memcpy(out+oi2,newp,nl); oi2+=nl; i+=ol; if(!global) done=1; } else out[oi2++]=ln[i++]; }
    while(i<sl && oi2<(int)sizeof(out)-1) out[oi2++]=ln[i++];
    out[oi2]=0; fputs(out,stdout);
  }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_tr(const char *s1,const char *s2){
  /* Expand ranges like a-z, A-Z, 0-9 in s1 and s2 */
  char exp1[256], exp2[256]; int e1=0, e2=0;
  for(int i=0;s1[i]&&e1<255;i++){ if(s1[i]=='-'&&i>0&&s1[i+1]&&e1>0){ char lo=exp1[e1-1], hi=s1[i+1]; for(char c=lo+1;c<=hi&&e1<255;c++) exp1[e1++]=c; i++; } else exp1[e1++]=s1[i]; }
  exp1[e1]=0;
  for(int i=0;s2[i]&&e2<255;i++){ if(s2[i]=='-'&&i>0&&s2[i+1]&&e2>0){ char lo=exp2[e2-1], hi=s2[i+1]; for(char c=lo+1;c<=hi&&e2<255;c++) exp2[e2++]=c; i++; } else exp2[e2++]=s2[i]; }
  exp2[e2]=0;
  /* Interpret escape sequences in exp1 and exp2 */
  char real1[256], real2[256]; int r1=0, r2=0;
  for(int i=0;exp1[i]&&r1<255;i++){ if(exp1[i]=='\\' && exp1[i+1]){ char n=exp1[i+1]; real1[r1++]=(n=='n')?'\n':(n=='t')?'\t':(n=='r')?'\r':n; i++; } else real1[r1++]=exp1[i]; }
  real1[r1]=0;
  for(int i=0;exp2[i]&&r2<255;i++){ if(exp2[i]=='\\' && exp2[i+1]){ char n=exp2[i+1]; real2[r2++]=(n=='n')?'\n':(n=='t')?'\t':(n=='r')?'\r':n; i++; } else real2[r2++]=exp2[i]; }
  real2[r2]=0;
  int c; int l2=r2;
  while((c=fgetc(stdin))!=EOF){
    const char *p=strchr(real1,c);
    if(p){ int idx=(int)(p-real1); putchar(idx<l2?real2[idx]:(l2>0?real2[l2-1]:c)); }
    else putchar(c);
  }
}
static void __attribute__((unused)) __b_tr_delete(const char *set){
  /* Expand ranges and escape sequences in set */
  char exp1[256]; int e1=0;
  for(int i=0;set[i]&&e1<255;i++){ if(set[i]=='-'&&i>0&&set[i+1]&&e1>0){ char lo=exp1[e1-1], hi=set[i+1]; for(char c=lo+1;c<=hi&&e1<255;c++) exp1[e1++]=c; i++; } else exp1[e1++]=set[i]; }
  exp1[e1]=0;
  char real1[256]; int r1=0;
  for(int i=0;exp1[i]&&r1<255;i++){ if(exp1[i]=='\\' && exp1[i+1]){ char n=exp1[i+1]; real1[r1++]=(n=='n')?'\n':(n=='t')?'\t':(n=='r')?'\r':n; i++; } else real1[r1++]=exp1[i]; }
  real1[r1]=0;
  int c;
  while((c=fgetc(stdin))!=EOF){
    if(!strchr(real1,c)) putchar(c);
  }
}
static void __attribute__((unused)) __b_cut(const char *fields,char delim,const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("cut");return;}
  char ln[8192];
  while(fgets(ln,sizeof(ln),f)){
    int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\n') ln[--l]=0;
    /* parse fields like 1,3 or 1-3 */
    char fc[256]; strncpy(fc,fields,sizeof(fc)-1); fc[sizeof(fc)-1]=0;
    char *tok=strtok(fc,","); int first=1;
    while(tok){ int a,b; if(strchr(tok,'-')){ sscanf(tok,"%d-%d",&a,&b); } else { a=b=atoi(tok); }
      int col=1; char *p=ln; char *start=p;
      while(*p){ if(*p==delim){ if(col>=a&&col<=b){ if(!first)putchar(delim); int len=(int)(p-start); printf("%.*s",len,start); first=0; } col++; start=p+1; } p++; }
      if(col>=a&&col<=b){ if(!first)putchar(delim); fputs(start,stdout); first=0; }
      tok=strtok(NULL,",");
    }
    putchar('\n');
  }
  if(path)fclose(f);
}
static int __sort_cmp(const void *a,const void *b){ return strcmp(*(const char**)a,*(const char**)b); }
static int __sort_cmp_rev(const void *a,const void *b){ return strcmp(*(const char**)b,*(const char**)a); }
static int __sort_cmp_num(const void *a,const void *b){ double da=atof(*(const char**)a),db=atof(*(const char**)b); return (da>db)-(da<db); }
static void __attribute__((unused)) __b_sort(const char *path,int rev,int uniq,int num){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("sort");return;}
  char **lines=NULL; int n=0,cap=0; char ln[8192];
  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){ cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*)); } lines[n++]=strdup(ln); }
  if(path)fclose(f);
  if(num) qsort(lines,n,sizeof(char*),rev?__sort_cmp_rev:__sort_cmp_num);
  else qsort(lines,n,sizeof(char*),rev?__sort_cmp_rev:__sort_cmp);
  const char *prev=NULL;
  for(int i=0;i<n;i++){ if(uniq&&prev&&strcmp(prev,lines[i])==0){ free(lines[i]); continue; } fputs(lines[i],stdout); prev=lines[i]; }
  for(int i=0;i<n;i++) free(lines[i]);
  free(lines);
}
static void __attribute__((unused)) __b_uniq(const char *path,int count){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("uniq");return;}
  char ln[8192]; char prev[8192]=""; int cnt=0,first=1;
  while(fgets(ln,sizeof(ln),f)){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\n') ln[--l]=0;
    if(first||strcmp(prev,ln)!=0){ if(!first){ if(count) printf("%7d %s\n",cnt,prev); else printf("%s\n",prev); } strcpy(prev,ln); cnt=1; first=0; }
    else cnt++;
  }
  if(!first){ if(count) printf("%7d %s\n",cnt,prev); else printf("%s\n",prev); }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_wc(const char *path,int dl,int dw,int dc){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("wc");return;}
  long li=0,wi=0,ci=0; int iw=0,c;
  while((c=fgetc(f))!=EOF){ci++;if(c=='\n')li++;if(isspace(c))iw=0;else if(!iw){iw=1;wi++;}}
  if(path)fclose(f);
  {int __w1=1;
  if(dl)printf("%s%ld",__w1?"":" ",li),__w1=0;
  if(dw)printf("%s%ld",__w1?"":" ",wi),__w1=0;
  if(dc)printf("%s%ld",__w1?"":" ",ci),__w1=0;
  if(path)printf("%s%s",__w1?"":" ",path);
  putchar('\n');
  }
}
static void __attribute__((unused)) __b_head(const char *path,int n){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("head");return;}
  char ln[8192]; int c=0; while(c<n&&fgets(ln,sizeof(ln),f)){fputs(ln,stdout);c++;}
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_tail(const char *path,int n){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("tail");return;}
  char **buf=(char**)malloc(n*sizeof(char*)); int i; for(i=0;i<n;i++)buf[i]=NULL;
  char ln[8192]; int idx=0,tot=0;
  while(fgets(ln,sizeof(ln),f)){free(buf[idx%n]);buf[idx%n]=strdup(ln);idx++;tot++;}
  if(path)fclose(f);
  int st=(tot>n)?idx%n:0,cnt=(tot>n)?n:tot;
  for(i=0;i<cnt;i++){int j=(st+i)%n;if(buf[j])fputs(buf[j],stdout);}
  for(i=0;i<n;i++)free(buf[i]);
  free(buf);
}
static void __attribute__((unused)) __b_tee(int append,...){
  va_list ap; va_start(ap,append);
  FILE **fps=NULL; int nf=0;
  const char *p;
  while((p=va_arg(ap,const char*))!=NULL){ fps=realloc(fps,(nf+1)*sizeof(FILE*)); fps[nf]=fopen(p,append?"a":"w"); nf++; }
  va_end(ap);
  int c; while((c=fgetc(stdin))!=EOF){ putchar(c); for(int i=0;i<nf;i++) if(fps[i]) fputc(c,fps[i]); }
  for(int i=0;i<nf;i++) if(fps[i]) fclose(fps[i]);
  free(fps);
}
static void __attribute__((unused)) __b_xargs(const char *first,...){
  va_list ap; va_start(ap,first);
  const char *extra[32]; int ne=0; const char *p;
  while((p=va_arg(ap,const char*))!=NULL && ne<31) extra[ne++]=p;
  va_end(ap);
  /* Build the xargs command */
  char full[16384]; int fl=0;
  fl+=snprintf(full+fl,sizeof(full)-fl,"xargs %s",first);
  for(int i=0;i<ne;i++){ fl+=snprintf(full+fl,sizeof(full)-fl," %s",extra[i]); }
  /* Pass stdin directly to xargs, reading all lines */
  char ln[8192]; char all_lines[65536]; int al=0;
  while(fgets(ln,sizeof(ln),stdin)){ int l=(int)strlen(ln); if(al+l<(int)sizeof(all_lines)-2){ memcpy(all_lines+al,ln,l); al+=l; } }
  all_lines[al]=0;
  /* Write to temp file and use as stdin */
  FILE *xf=tmpfile(); if(xf){ fputs(all_lines,xf); fflush(xf); int xfd=fileno(xf); lseek(xfd,0,SEEK_SET);
    int saved=dup(0); dup2(xfd,0);
    system(full);
    dup2(saved,0); close(saved); fclose(xf);
  } else system(full);
}
static void __attribute__((unused)) __b_rev(const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("rev");return;}
  char ln[8192]; while(fgets(ln,sizeof(ln),f)){ int l=(int)strlen(ln); if(l>0&&ln[l-1]=='\n') ln[--l]=0; for(int i=l-1;i>=0;i--) putchar(ln[i]); putchar('\n'); }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_tac(const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("tac");return;}
  char **lines=NULL; int n=0,cap=0; char ln[8192];
  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));} lines[n++]=strdup(ln); }
  if(path)fclose(f);
  for(int i=n-1;i>=0;i--){ fputs(lines[i],stdout); free(lines[i]); }
  free(lines);
}
static void __attribute__((unused)) __b_nl(const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("nl");return;}
  char ln[8192]; int n=1; while(fgets(ln,sizeof(ln),f)){ printf("%6d  %s",n,ln); n++; }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_fold(const char *path,int w){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("fold");return;}
  int c,col=0; while((c=fgetc(f))!=EOF){ putchar(c); if(c=='\n') col=0; else { col++; if(col>=w){ putchar('\n'); col=0; } } }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_paste(const char *first,...){
  va_list ap; va_start(ap,first);
  FILE *fps[16]; const char *names[16]; int nf=0;
  if(first){ names[nf]=first; fps[nf]=fopen(first,"r"); nf++; }
  const char *p; while((p=va_arg(ap,const char*))!=NULL && nf<16){ names[nf]=p; fps[nf]=fopen(p,"r"); nf++; }
  va_end(ap);
  if(nf==0) return;
  char lns[16][8192]; int alive=1;
  while(alive){ alive=0; for(int i=0;i<nf;i++){ if(fps[i]&&fgets(lns[i],sizeof(lns[i]),fps[i])){ alive=1; int l=(int)strlen(lns[i]); if(l>0&&lns[i][l-1]=='\n') lns[i][--l]=0; } else lns[i][0]=0; if(i>0) putchar('\t'); fputs(lns[i],stdout); } putchar('\n'); }
  for(int i=0;i<nf;i++) if(fps[i]) fclose(fps[i]);
}
static void __attribute__((unused)) __b_expand(const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("expand");return;}
  int c,col=0; while((c=fgetc(f))!=EOF){ if(c=='\t'){ do{ putchar(' '); col++; }while(col%8); } else { putchar(c); if(c=='\n') col=0; else col++; } }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_unexpand(const char *path){
  FILE *f=path?fopen(path,"r"):stdin; if(!f){perror("unexpand");return;}
  int c,col=0,spaces=0; while((c=fgetc(f))!=EOF){ if(c==' '){ spaces++; col++; if(col%8==0){ putchar('\t'); spaces=0; } } else { while(spaces>0){ putchar(' '); spaces--; } putchar(c); if(c=='\n') col=0; else col++; } }
  if(path)fclose(f);
}
static void __attribute__((unused)) __b_column(const char *first,...){
  va_list ap; va_start(ap,first); const char *p; FILE *f=first?fopen(first,"r"):stdin; while((p=va_arg(ap,const char*))!=NULL){} va_end(ap);
  if(!f){perror("column");return;}
  char ln[8192]; while(fgets(ln,sizeof(ln),f)){ fputs(ln,stdout); }
  if(first)fclose(f);
}
static void __attribute__((unused)) __b_shuf(const char *first,...){
  va_list ap; va_start(ap,first); const char *p; FILE *f=first?fopen(first,"r"):stdin; while((p=va_arg(ap,const char*))!=NULL){} va_end(ap);
  if(!f){perror("shuf");return;}
  char **lines=NULL; int n=0,cap=0; char ln[8192];
  while(fgets(ln,sizeof(ln),f)){ if(n>=cap){cap=cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*));} lines[n++]=strdup(ln); }
  if(first)fclose(f);
  srand((unsigned)time(NULL));
  for(int i=n-1;i>0;i--){ int j=rand()%(i+1); char *t=lines[i]; lines[i]=lines[j]; lines[j]=t; }
  for(int i=0;i<n;i++){ fputs(lines[i],stdout); free(lines[i]); } free(lines);
}
static void __attribute__((unused)) __b_comm(const char *a,const char *b){
  FILE *fa=fopen(a,"r"),*fb=fopen(b,"r"); if(!fa||!fb){perror("comm");return;}
  char la[8192],lb[8192]; int ha=1,hb=1;
  ha=fgets(la,sizeof(la),fa)?1:0; hb=fgets(lb,sizeof(lb),fb)?1:0;
  while(ha||hb){
    if(ha&&hb){ int l=(int)strlen(la); if(l>0&&la[l-1]=='\n')la[--l]=0; l=(int)strlen(lb); if(l>0&&lb[l-1]=='\n')lb[--l]=0;
      int c=strcmp(la,lb); if(c<0){ printf("%s\n",la); ha=fgets(la,sizeof(la),fa)?1:0; } else if(c>0){ printf("\t%s\n",lb); hb=fgets(lb,sizeof(lb),fb)?1:0; } else { printf("\t\t%s\n",la); ha=fgets(la,sizeof(la),fa)?1:0; hb=fgets(lb,sizeof(lb),fb)?1:0; } }
    else if(ha){ fputs(la,stdout); ha=fgets(la,sizeof(la),fa)?1:0; } else { printf("\t%s",lb); hb=fgets(lb,sizeof(lb),fb)?1:0; }
  }
  fclose(fa); fclose(fb);
}
static void __attribute__((unused)) __b_diff(const char *a,const char *b){
  FILE *fa=fopen(a,"r"), *fb=fopen(b,"r");
  if(!fa||!fb){ if(fa)fclose(fa); if(fb)fclose(fb); __exit_status=2; return; }
  int la=0,lb=0,ca,cb,diff=0;
  while(1){
    ca=fgetc(fa); cb=fgetc(fb);
    if(ca==EOF&&cb==EOF) break;
    if(ca!=cb){ diff=1; break; }
    if(ca=='\n') la++;
    if(cb=='\n') lb++;
  }
  fclose(fa); fclose(fb);
  __exit_status=diff?1:0;
}

/* ---- search builtins ---- */
static void __attribute__((unused)) __b_find(const char *first,...){
  va_list ap; va_start(ap,first);
  char cmd[8192]="find"; const char *p=first;
  while(p){ strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); p=va_arg(ap,const char*); }
  va_end(ap); system(cmd);
}
static void __attribute__((unused)) __b_which(const char *cmd){ char *path=getenv("PATH"); if(!path){return;} char *p=strdup(path); char *tok=strtok(p,":"); while(tok){ char full[4096]; snprintf(full,sizeof(full),"%s/%s",tok,cmd); if(access(full,X_OK)==0){ printf("%s\n",full); free(p); return; } tok=strtok(NULL,":"); } free(p); }
static void __attribute__((unused)) __b_whereis(const char *cmd){ char cmd2[1024]; snprintf(cmd2,sizeof(cmd2),"whereis %s",cmd); system(cmd2); }
static void __attribute__((unused)) __b_locate(const char *pat){ char cmd[1024]; snprintf(cmd,sizeof(cmd),"locate %s",pat); system(cmd); }

/* ---- system info builtins ---- */
static void __attribute__((unused)) __b_date(const char *fmt){
  time_t t=time(NULL); struct tm *tm=localtime(&t);
  if(!fmt||!fmt[0]){ char b[256]; strftime(b,sizeof(b),"%a %b %e %H:%M:%S %Z %Y",tm); printf("%s\n",b); return; }
  if(fmt[0]=='+'){ char b[1024]; strftime(b,sizeof(b),fmt+1,tm); printf("%s\n",b); }
}
static void __attribute__((unused)) __b_whoami(void){ struct passwd *pw=getpwuid(getuid()); printf("%s\n",pw?pw->pw_name:"unknown"); }
static void __attribute__((unused)) __b_hostname(void){ char h[256]; if(gethostname(h,sizeof(h))==0) printf("%s\n",h); }
static void __attribute__((unused)) __b_hostname_set(const char *h){ if(sethostname(h,strlen(h))!=0) perror("hostname"); }
static void __attribute__((unused)) __b_uname(int all){ struct utsname u; if(uname(&u)!=0){perror("uname");return;} if(all) printf("%s %s %s %s %s\n",u.sysname,u.nodename,u.release,u.version,u.machine); else printf("%s\n",u.sysname); }
static void __attribute__((unused)) __b_arch(void){ struct utsname u; uname(&u); printf("%s\n",u.machine); }
static void __attribute__((unused)) __b_nproc(void){ long n=sysconf(_SC_NPROCESSORS_ONLN); printf("%ld\n",n); }
static void __attribute__((unused)) __b_id(void){ printf("uid=%d gid=%d\n",getuid(),getgid()); }
static void __attribute__((unused)) __b_env(void){ extern char **environ; for(char **e=environ;*e;e++) printf("%s\n",*e); }
static void __attribute__((unused)) __b_ps(void){ system("ps"); }
static void __attribute__((unused)) __b_kill(const char *first,...){ va_list ap; va_start(ap,first); const char *p=first; char cmd[1024]="kill"; while(p){ strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); p=va_arg(ap,const char*); } va_end(ap); system(cmd); }
static void __attribute__((unused)) __b_wait(void){ int st; while(waitpid(-1,&st,WNOHANG)>0); }
static void __attribute__((unused)) __b_jobs(void){ /* stub */ }
static void __attribute__((unused)) __b_bg(void){ /* stub */ }
static void __attribute__((unused)) __b_fg(void){ /* stub */ }
static const char *__sh_trap_actions[32];
static int __sh_trap_is_func[32]={0};
static void (*__sh_trap_func[32])(int,char**)={NULL};
static void __b_trap_run(int sig){ if(sig>=0&&sig<32&&__sh_trap_actions[sig]&&__sh_trap_actions[sig][0]){ if(__sh_trap_is_func[sig]&&__sh_trap_func[sig]){ __sh_trap_func[sig](0,NULL); } else { system(__sh_trap_actions[sig]); } } }
static void __b_trap_exit_handler(void){ __b_trap_run(0); }
static void __b_trap_sighandler(int sig){ __b_trap_run(sig); }
static void __attribute__((unused)) __b_trap(const char *action,int sig){ if(!action||action[0]==0) return; if(sig<0||sig>=32) return; __sh_trap_actions[sig]=action; if(sig==0) atexit(__b_trap_exit_handler); else signal(sig,__b_trap_sighandler); }
static void __attribute__((unused)) __b_type(const char *cmd){ printf("%s is a shell builtin\n",cmd); }
static void __attribute__((unused)) __b_command(const char *first,...){ va_list ap; va_start(ap,first); char cmd[8192]; snprintf(cmd,sizeof(cmd),"%s",first); const char *p; while((p=va_arg(ap,const char*))!=NULL){ strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); } va_end(ap); system(cmd); }
static void __attribute__((unused)) __b_alias(const char *a){ if(!a){ system("alias"); return; } }
static void __attribute__((unused)) __b_unalias(const char *a){ (void)a; }
static void __attribute__((unused)) __b_history(void){ /* stub */ }
static void __attribute__((unused)) __b_pushd(const char *d){ char cwd[4096]; getcwd(cwd,sizeof(cwd)); chdir(d); }
static void __attribute__((unused)) __b_popd(void){ /* stub */ }
static void __attribute__((unused)) __b_dirs(void){ char cwd[4096]; getcwd(cwd,sizeof(cwd)); printf("%s\n",cwd); }
static void __attribute__((unused)) __b_seq(int s,int st,int e){ if(st>0) for(int i=s;i<=e;i+=st) printf("%d\n",i); else for(int i=s;i>=e;i+=st) printf("%d\n",i); }
static void __attribute__((unused)) __b_yes(const char *msg){ while(1){ printf("%s\n",msg?msg:"y"); fflush(stdout); } }
static void __attribute__((unused)) __b_source(const char *f){
  if(!f||!*f) return;
  FILE *fp=fopen(f,"r");
  if(!fp) return;
  char line[8192];
  while(fgets(line,sizeof(line),fp)){
    int l=(int)strlen(line);
    if(l>0&&line[l-1]=='\n') line[l-1]=0;
    if(!line[0]||line[0]=='#') continue;
    /* try to handle VAR=value assignments inline (no system call) */
    char *eq=strchr(line,'=');
    if(eq && eq>line){
      /* check it's a simple assignment (no spaces before =) */
      char *sp=strchr(line,' ');
      if(!sp || sp>eq){
        *eq=0; char *val=eq+1;
        /* strip surrounding quotes from val */
        int vl=(int)strlen(val);
        if(vl>=2 && ((val[0]=='"'&&val[vl-1]=='"')||(val[0]=='\''&&val[vl-1]=='\''))){
          val[vl-1]=0; val++;
        }
        setenv(line,val,1);
        continue;
      }
    }
    /* for non-assignment lines, fall back to system */
    system(line);
  }
  fclose(fp);
}
static void __attribute__((unused)) __b_eval(const char **a){ char cmd[8192]=""; for(const char **p=a;*p;p++){ if(*cmd) strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,*p,sizeof(cmd)-strlen(cmd)-1); } system(cmd); }
static void __attribute__((unused)) __b_nohup(const char *first,...){ va_list ap; va_start(ap,first); char cmd[8192]; snprintf(cmd,sizeof(cmd),"nohup %s",first); const char *p; while((p=va_arg(ap,const char*))!=NULL){ strncat(cmd," ",sizeof(cmd)-strlen(cmd)-1); strncat(cmd,p,sizeof(cmd)-strlen(cmd)-1); } va_end(ap); strncat(cmd," >/dev/null 2>&1 &",sizeof(cmd)-strlen(cmd)-1); system(cmd); }
static void __attribute__((unused)) __b_time(const char *cmd){ clock_t t0=clock(); system(cmd); clock_t t1=clock(); fprintf(stderr,"real %.3fs\n",(double)(t1-t0)/CLOCKS_PER_SEC); }
static void __attribute__((unused)) __b_free_mem(void){ system("free"); }
static void __attribute__((unused)) __b_uptime(void){ system("uptime"); }
static void __attribute__((unused)) __b_who(void){ system("who"); }
static void __attribute__((unused)) __b_last(void){ system("last"); }
static void __attribute__((unused)) __b_dmesg(void){ system("dmesg"); }
static void __attribute__((unused)) __b_lsof(void){ system("lsof"); }
static void __attribute__((unused)) __b_mount(void){ system("mount"); }
static void __attribute__((unused)) __b_tty(void){ char *t=ttyname(0); printf("%s\n",t?t:"not a tty"); }
static void __attribute__((unused)) __b_help(void){
  printf("shell2c runtime — supported builtins:\n");
  printf("  echo printf cd pwd ls cat grep sed tr cut sort uniq wc head tail\n");
  printf("  cp mv rm mkdir rmdir touch ln chmod chown stat du df file\n");
  printf("  basename dirname realpath readlink mktemp install\n");
  printf("  tee xargs rev tac nl fold paste expand unexpand column shuf comm diff\n");
  printf("  find which whereis locate\n");
  printf("  date whoami hostname uname id env export unset set source eval\n");
  printf("  ps kill sleep wait jobs bg fg trap type command alias unalias\n");
  printf("  history pushd popd dirs seq yes true false test [ expr read exit\n");
  printf("  return clear reset nohup time free uptime who last dmesg lsof\n");
  printf("  mount arch nproc tty help\n");
}
/* ---- end runtime ---- */


#endif
