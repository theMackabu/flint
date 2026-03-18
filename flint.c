#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>

#define PREFIX    0xFFF0000000000000ULL
#define DATA_MASK 0x00007FFFFFFFFFFFULL
enum Type { T_NUM, T_STR, T_BOOL, T_NIL, T_ARR, T_OBJ };

static inline uint64_t mknum(double d)           { uint64_t v; memcpy(&v, &d, 8); return v; }
static inline double asnum(uint64_t v)           { double d; memcpy(&d, &v, 8); return d; }
static inline uint64_t mktag(int t, uint64_t d)  { return PREFIX | ((uint64_t)t << 47) | (d & DATA_MASK); }
static inline int  vtag(uint64_t v)              { return v > PREFIX ? (v >> 47) & 0x1F : T_NUM; }
static inline uint64_t vdata(uint64_t v)         { return v & DATA_MASK; }
static inline uint64_t mkstr(uint64_t id)        { return mktag(T_STR, id); }
static inline uint64_t mkbool(int b)             { return mktag(T_BOOL, b); }
static inline uint64_t mknil(void)               { return mktag(T_NIL, 0); }
static inline uint64_t mkarr(int id)             { return mktag(T_ARR, id); }
static inline uint64_t mkobj(int id)             { return mktag(T_OBJ, id); }
static inline int isfalsy(uint64_t v)            { return vtag(v)==T_NIL || (vtag(v)==T_BOOL && !vdata(v)) || (vtag(v)==T_NUM && asnum(v)==0); }

static char *strtab[4096]; static int nstrs; static int intern(char *s) {
  for (int i = 0; i < nstrs; i++) if (!strcmp(strtab[i], s)) return i;
  strtab[nstrs] = strdup(s); return nstrs++;
}

typedef struct { uint64_t *data; int len, cap; } Arr;
typedef struct { int *keys; uint64_t *vals; int len, cap; } Obj;
static Arr arrs[4096]; static int narrs;
static Obj objs[4096]; static int nobjs;

static int new_arr(void) { int id = narrs++; arrs[id] = (Arr){NULL, 0, 0}; return id; }
static int new_obj(void) { int id = nobjs++; objs[id] = (Obj){NULL, NULL, 0, 0}; return id; }

static void arr_push(Arr *a, uint64_t v) {
  if (a->len >= a->cap) { a->cap = a->cap ? a->cap*2 : 8; a->data = realloc(a->data, a->cap*sizeof(uint64_t)); }
  a->data[a->len++] = v;
}

static void obj_set(Obj *o, int key, uint64_t val) {
  for (int i = 0; i < o->len; i++) if (o->keys[i] == key) { o->vals[i] = val; return; }
  if (o->len >= o->cap) { o->cap = o->cap ? o->cap*2 : 8; o->keys = realloc(o->keys, o->cap*sizeof(int)); o->vals = realloc(o->vals, o->cap*sizeof(uint64_t)); }
  o->keys[o->len] = key; o->vals[o->len] = val; o->len++;
}

static uint64_t obj_get(Obj *o, int key) {
  for (int i = 0; i < o->len; i++) if (o->keys[i] == key) return o->vals[i];
  return mknil();
}

static void print_val(uint64_t v, FILE *fp) {
  switch (vtag(v)) {
  case T_NUM:  { double d = asnum(v); d == (int)d ? fprintf(fp,"%d",(int)d) : fprintf(fp,"%g",d); } break;
  case T_STR:  fprintf(fp, "%s", strtab[vdata(v)]); break; case T_BOOL: fprintf(fp, "%s", vdata(v) ? "true" : "false"); break;
  case T_NIL:  fprintf(fp, "nil"); break; case T_ARR:  { Arr *a = &arrs[vdata(v)]; fprintf(fp, "[");
    for (int i = 0; i < a->len; i++) { if (i) fprintf(fp, ", "); if (vtag(a->data[i])==T_STR) { fprintf(fp,"\""); print_val(a->data[i],fp); fprintf(fp,"\""); } else print_val(a->data[i], fp); }
    fprintf(fp, "]"); } break; case T_OBJ:  { Obj *o = &objs[vdata(v)]; fprintf(fp, "{");
    for (int i = 0; i < o->len; i++) { if (i) fprintf(fp, ", "); fprintf(fp, "%s: ", strtab[o->keys[i]]); if (vtag(o->vals[i])==T_STR) { fprintf(fp,"\""); print_val(o->vals[i],fp); fprintf(fp,"\""); } else print_val(o->vals[i], fp); }
    fprintf(fp, "}"); } break;
  }
}

static uint64_t b_print(uint64_t *args, int n) { for (int i = 0; i < n; i++) { if (i) printf(" "); print_val(args[i], stdout); } return mknil(); }
static uint64_t b_println(uint64_t *args, int n) { b_print(args, n); printf("\n"); return mknil(); }
static uint64_t b_exit(uint64_t *args, int n) { exit(n > 0 && vtag(args[0]) == T_NUM ? (int)asnum(args[0]) : 0); }

static uint64_t b_len(uint64_t *args, int n) {
  (void)n; if (vtag(args[0])==T_STR) return mknum(strlen(strtab[vdata(args[0])]));
  if (vtag(args[0])==T_ARR) return mknum(arrs[vdata(args[0])].len);
  if (vtag(args[0])==T_OBJ) return mknum(objs[vdata(args[0])].len);
  return mknum(0);
}

static uint64_t b_type(uint64_t *args, int n) {
  (void)n; const char *names[] = {"num","str","bool","nil","arr","obj"};
  return mkstr(intern((char*)names[vtag(args[0])]));
}

static uint64_t b_str(uint64_t *args, int n) {
  (void)n; char buf[4096]; switch (vtag(args[0])) {
  case T_NUM: { double d=asnum(args[0]); d==(int)d ? snprintf(buf,sizeof buf,"%d",(int)d) : snprintf(buf,sizeof buf,"%g",d); break; }
  case T_STR: return args[0]; case T_BOOL: return mkstr(intern(vdata(args[0]) ? "true" : "false")); case T_NIL:  return mkstr(intern("nil"));
  case T_ARR: case T_OBJ: { FILE *fp = fmemopen(buf, sizeof buf, "w"); print_val(args[0], fp); fclose(fp); break; }
  } return mkstr(intern(buf));
}

static uint64_t b_num(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) == T_STR) return mknum(strtod(strtab[vdata(args[0])], NULL));
  if (vtag(args[0]) == T_BOOL) return mknum(vdata(args[0]));
  return args[0];
}

static uint64_t b_exec(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return mknil();
  FILE *fp = popen(strtab[vdata(args[0])], "r"); if (!fp) return mknil();
  char buf[65536]; int len = fread(buf, 1, sizeof(buf)-1, fp); pclose(fp);
  while (len > 0 && buf[len-1] == '\n') len--;
  buf[len] = 0; return mkstr(intern(buf));
}

static uint64_t b_fetch(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return mknil();
  char cmd[4096]; snprintf(cmd, sizeof cmd, "curl -sfL '%s'", strtab[vdata(args[0])]);
  FILE *fp = popen(cmd, "r"); if (!fp) return mknil();
  char buf[65536]; int len = fread(buf, 1, sizeof(buf)-1, fp); pclose(fp);
  buf[len] = 0; return mkstr(intern(buf));
}

static uint64_t b_pipe(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR || vtag(args[1]) != T_STR) return mknil();
  int in_fd[2], out_fd[2];
  if (pipe(in_fd) < 0 || pipe(out_fd) < 0) return mknil();
  pid_t pid = fork();
  if (pid < 0) return mknil();
  if (pid == 0) {
    close(in_fd[1]); close(out_fd[0]);
    dup2(in_fd[0], 0); dup2(out_fd[1], 1);
    close(in_fd[0]); close(out_fd[1]);
    execl("/bin/sh", "sh", "-c", strtab[vdata(args[1])], NULL); _exit(1);
  }
  close(in_fd[0]); close(out_fd[1]);
  const char *data = strtab[vdata(args[0])];
  write(in_fd[1], data, strlen(data)); close(in_fd[1]);
  char buf[65536]; int total = 0, r;
  while ((r = read(out_fd[0], buf + total, sizeof(buf)-1-total)) > 0) total += r;
  close(out_fd[0]); waitpid(pid, NULL, 0);
  while (total > 0 && buf[total-1] == '\n') total--;
  buf[total] = 0; return mkstr(intern(buf));
}

static uint64_t b_eprintln(uint64_t *args, int n) {
  for (int i = 0; i < n; i++) { if (i) fprintf(stderr, " "); print_val(args[i], stderr); }
  fprintf(stderr, "\n"); return mknil();
}

static uint64_t b_env(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return mknil();
  char *v = getenv(strtab[vdata(args[0])]); return v ? mkstr(intern(v)) : mknil();
}

static uint64_t b_setenv(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR || vtag(args[1]) != T_STR) return mkbool(0);
  return mkbool(setenv(strtab[vdata(args[0])], strtab[vdata(args[1])], 1) == 0);
}

static uint64_t b_fork(uint64_t *args, int n) {
  (void)args; (void)n; fflush(stdout); fflush(stderr);
  pid_t pid = fork(); return pid < 0 ? mknil() : mknum(pid);
}

static uint64_t b_wait(uint64_t *args, int n) {
  (void)args; (void)n; int status; pid_t pid = wait(&status);
  return pid < 0 ? mknil() : mknum(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}

static uint64_t b_read_file(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return mknil();
  FILE *fp = fopen(strtab[vdata(args[0])], "r"); if (!fp) return mknil();
  char buf[65536]; int len = fread(buf, 1, sizeof(buf)-1, fp); fclose(fp);
  buf[len] = 0; return mkstr(intern(buf));
}

static uint64_t b_write_file(uint64_t *args, int n) {
  (void)n; if (n < 2 || vtag(args[0]) != T_STR || vtag(args[1]) != T_STR) return mkbool(0);
  FILE *fp = fopen(strtab[vdata(args[0])], "w"); if (!fp) return mkbool(0);
  fprintf(fp, "%s", strtab[vdata(args[1])]); fclose(fp); return mkbool(1);
}

static uint64_t b_trim(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return args[0];
  char *s = strtab[vdata(args[0])], *e; while (isspace(*s)) s++;
  e = s + strlen(s); while (e > s && isspace(e[-1])) e--;
  char buf[2048]; int len = e - s; memcpy(buf, s, len); buf[len] = 0;
  return mkstr(intern(buf));
}

static uint64_t b_substr(uint64_t *args, int n) {
  if (vtag(args[0]) != T_STR) return mknil();
  char *s = strtab[vdata(args[0])]; int slen = strlen(s);
  int start = n > 1 ? (int)asnum(args[1]) : 0;
  int count = n > 2 ? (int)asnum(args[2]) : slen - start;
  if (start < 0) start = 0; if (start >= slen) return mkstr(intern(""));
  if (count > slen - start) count = slen - start;
  char buf[2048]; memcpy(buf, s + start, count); buf[count] = 0;
  return mkstr(intern(buf));
}

static uint64_t b_index_of(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR || vtag(args[1]) != T_STR) return mknum(-1);
  char *pos = strstr(strtab[vdata(args[0])], strtab[vdata(args[1])]);
  return pos ? mknum(pos - strtab[vdata(args[0])]) : mknum(-1);
}

static uint64_t b_replace(uint64_t *args, int n) {
  (void)n; if (vtag(args[0])!=T_STR || vtag(args[1])!=T_STR || vtag(args[2])!=T_STR) return args[0];
  char *s = strtab[vdata(args[0])], *old = strtab[vdata(args[1])], *rep = strtab[vdata(args[2])];
  int olen = strlen(old); if (!olen) return args[0];
  char buf[4096]; char *w = buf;
  while (*s) { char *f = strstr(s, old);
    if (!f) { while (*s) *w++ = *s++; } else { memcpy(w, s, f-s); w += f-s; int rlen=strlen(rep); memcpy(w, rep, rlen); w += rlen; s = f + olen; }
  } *w = 0; return mkstr(intern(buf));
}

static uint64_t b_upper(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return args[0];
  char buf[2048]; char *s = strtab[vdata(args[0])]; int i = 0;
  while (s[i]) { buf[i] = toupper(s[i]); i++; } buf[i] = 0;
  return mkstr(intern(buf));
}

static uint64_t b_lower(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_STR) return args[0];
  char buf[2048]; char *s = strtab[vdata(args[0])]; int i = 0;
  while (s[i]) { buf[i] = tolower(s[i]); i++; } buf[i] = 0;
  return mkstr(intern(buf));
}

static uint64_t b_push(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_ARR) return mknil();
  arr_push(&arrs[vdata(args[0])], args[1]); return args[0];
}

static uint64_t b_pop(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_ARR) return mknil();
  Arr *a = &arrs[vdata(args[0])]; return a->len > 0 ? a->data[--a->len] : mknil();
}

static uint64_t b_keys(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_OBJ) return mkarr(new_arr());
  Obj *o = &objs[vdata(args[0])]; int id = new_arr();
  for (int i = 0; i < o->len; i++) arr_push(&arrs[id], mkstr(o->keys[i]));
  return mkarr(id);
}

static uint64_t b_values(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_OBJ) return mkarr(new_arr());
  Obj *o = &objs[vdata(args[0])]; int id = new_arr();
  for (int i = 0; i < o->len; i++) arr_push(&arrs[id], o->vals[i]);
  return mkarr(id);
}

static uint64_t b_has(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) != T_OBJ || vtag(args[1]) != T_STR) return mkbool(0);
  Obj *o = &objs[vdata(args[0])]; int key = vdata(args[1]);
  for (int i = 0; i < o->len; i++) if (o->keys[i] == key) return mkbool(1);
  return mkbool(0);
}

typedef uint64_t (*Builtin)(uint64_t*, int);
static struct { char *name; Builtin fn; } builtins[] = {
  {"print", b_print}, {"println", b_println}, {"eprintln", b_eprintln}, {"len", b_len}, {"type", b_type}, {"str", b_str}, {"num", b_num}, {"exec", b_exec}, {"fetch", b_fetch}, {"pipe", b_pipe}, {"exit", b_exit}, {"env", b_env}, {"setenv", b_setenv}, {"fork", b_fork}, {"wait", b_wait}, {"read_file", b_read_file}, {"write_file", b_write_file}, {"trim", b_trim}, {"substr", b_substr}, {"index_of", b_index_of}, {"replace", b_replace}, {"upper", b_upper}, {"lower", b_lower}, {"push", b_push}, {"pop", b_pop}, {"keys", b_keys}, {"values", b_values}, {"has", b_has},
};

#define NBUILTINS (sizeof builtins / sizeof builtins[0])
static int find_builtin(char *name) { for (int i = 0; i < (int)NBUILTINS; i++) if (!strcmp(builtins[i].name, name)) return i; return -1; }

static struct { char name[64]; char params[16][64]; int nparam; int addr; int slots[16]; int sbase, scount; } funcs[256];
static int nfuncs; static int find_func(char *name) { for (int i = 0; i < nfuncs; i++) if (!strcmp(funcs[i].name, name)) return i; return -1; }

enum Tok { TNUM=128, TSTR, TID, TIF, TELSE, TWHILE, TTRUE, TFALSE, TNIL, TEQ, TNE, TLE, TGE, TFN, TRET, TLET, TCONST, TEXEC, TTMPL, TDELETE, TEOF };
static char src[65536], *p; static int tok; static double toknum; static char tokstr[1024], tokname[64];

static void next(void) {
  while (isspace(*p)) p++;
  if (!*p) { tok = TEOF; return; }
  if (*p == '/' && p[1] == '/') { while (*p && *p != '\n') p++; next(); return; }
  if (isdigit(*p) || (*p == '.' && isdigit(p[1]))) { tok = TNUM; toknum = strtod(p, &p); return; }
  if (*p == '"' || *p == '\'') {
    char q = *p++; char *s = tokstr;
    while (*p && *p != q) { if (*p == '\\') { p++; *s++ = *p=='n' ? '\n' : *p=='t' ? '\t' : *p; p++; } else *s++ = *p++; }
    *s = 0; if (*p == q) p++; tok = TSTR; return;
  }
  if (isalpha(*p) || *p == '_') {
    char *s = tokname; while (isalnum(*p) || *p == '_') *s++ = *p++; *s = 0; tok = TID;
    if (!strcmp(tokname,"if")) tok=TIF; else if (!strcmp(tokname,"else")) tok=TELSE;
    else if (!strcmp(tokname,"while")) tok=TWHILE; else if (!strcmp(tokname,"function")) tok=TFN;
    else if (!strcmp(tokname,"return")) tok=TRET; else if (!strcmp(tokname,"let")) tok=TLET;
    else if (!strcmp(tokname,"const")) tok=TCONST; else if (!strcmp(tokname,"delete")) tok=TDELETE;
    else if (!strcmp(tokname,"true")) tok=TTRUE; else if (!strcmp(tokname,"false")) tok=TFALSE;
    else if (!strcmp(tokname,"nil")) tok=TNIL; return;
  }
  if (*p == '$' && p[1] == '`') {
    p += 2; char *s = tokstr; while (*p && *p != '`') *s++ = *p++;
    *s = 0; if (*p == '`') p++; tok = TEXEC; return;
  }
  if (*p == '`') { p++; tok = TTMPL; return; }
  char c = *p++;
  if (c=='=' && *p=='=') { p++; tok=TEQ; } else if (c=='!' && *p=='=') { p++; tok=TNE; }
  else if (c=='<' && *p=='=') { p++; tok=TLE; } else if (c=='>' && *p=='=') { p++; tok=TGE; } else tok = c;
}

enum Op { OPUSH, OLOAD, OSTORE, OADD, OSUB, OMUL, ODIV, OMOD, OEQ, ONE, OLT, OGT, OLE, OGE, OJMP, OJNZ, OJEZ, OHALT, ONEG, ONOT, OCALL, OCALL_U, ORET, OPOP, OARR, OOBJ, OIDX, OIDX_SET, ODEL };
enum Nd { NNUM, NSTR, NBOOL, NNIL, NID, NBINOP, NUNOP, NASSIGN, NIF, NWHILE, NCALL, NBLOCK, NFUNC, NRET, NARR, NOBJ, NINDEX, NIDX_ASSIGN, NDELETE };

typedef struct Node {
  int kind, op; double num; int strid, bval; char name[64];
  struct Node *a, *b, *c; struct Node *args[16]; int nargs; struct Node *stmts[256]; int nstmts;
} Node;

static void expect(int t)  { if (tok != t) { fprintf(stderr, "expected '%c' got %d\n", t, tok); exit(1); } next(); }
static Node *mknode(int k) { Node *n = calloc(1, sizeof(Node)); n->kind = k; return n; } static Node *postfix(void); static Node *expr(void);

static Node *atom(void) {
  if (tok == TNUM)   { Node *n = mknode(NNUM); n->num = toknum; next(); return n; }
  if (tok == TSTR)   { Node *n = mknode(NSTR); n->strid = intern(tokstr); next(); return n; }
  if (tok == TTRUE)  { Node *n = mknode(NBOOL); n->bval = 1; next(); return n; }
  if (tok == TFALSE) { Node *n = mknode(NBOOL); n->bval = 0; next(); return n; }
  if (tok == TNIL)   { Node *n = mknode(NNIL); next(); return n; }
  if (tok == TEXEC) {
    Node *cmd = mknode(NSTR); cmd->strid = intern(tokstr); next();
    Node *n = mknode(NCALL); strcpy(n->name, "exec"); n->args[n->nargs++] = cmd; return n;
  }
  if (tok == TTMPL) {
    Node *res = NULL; char buf[1024]; int bl = 0;
    #define ADDNODE(nd) do { if (res) { Node *a=mknode(NBINOP); a->op=OADD; a->a=res; a->b=(nd); res=a; } else res=(nd); } while(0)
    #define FLUSH() do { if (bl) { buf[bl]=0; Node *s=mknode(NSTR); s->strid=intern(buf); ADDNODE(s); bl=0; } } while(0)
    while (*p && *p != '`') {
      if (*p == '$' && p[1] == '{') {
        FLUSH(); p += 2; next(); Node *e = expr();
        Node *c = mknode(NCALL); strcpy(c->name, "str"); c->args[c->nargs++] = e; ADDNODE(c);
      } else if (*p == '\\') { p++; buf[bl++] = *p=='n' ? '\n' : *p=='t' ? '\t' : *p; p++;
      } else buf[bl++] = *p++;
    }
    FLUSH(); if (*p == '`') p++; next();
    if (!res) { res = mknode(NSTR); res->strid = intern(""); }
    return res;
    #undef FLUSH
    #undef ADDNODE
  }
  if (tok == TID) {
    char name[64]; strcpy(name, tokname); next(); if (tok == '(') {
      next(); Node *n = mknode(NCALL); strcpy(n->name, name);
      if (tok != ')') { n->args[n->nargs++] = expr(); while (tok == ',') { next(); n->args[n->nargs++] = expr(); } }
      expect(')'); return n;
    } Node *n = mknode(NID); strcpy(n->name, name); return n;
  }
  if (tok == '[') {
    next(); Node *n = mknode(NARR);
    if (tok != ']') { n->args[n->nargs++] = expr(); while (tok == ',') { next(); n->args[n->nargs++] = expr(); } }
    expect(']'); return n;
  }
  if (tok == '{') {
    next(); Node *n = mknode(NOBJ);
    if (tok != '}') {
      for (;;) {
        Node *k = mknode(NSTR);
        if (tok == TID) { k->strid = intern(tokname); next(); }
        else if (tok == TSTR) { k->strid = intern(tokstr); next(); }
        expect(':'); n->args[n->nargs++] = k; n->args[n->nargs++] = expr();
        if (tok != ',') break; next();
      }
    } expect('}'); return n;
  }
  if (tok == '(') { next(); Node *n = expr(); expect(')'); return n; }
  if (tok == '-') { next(); Node *n = mknode(NUNOP); n->op = '-'; n->a = atom(); return n; }
  if (tok == '!') { next(); Node *n = mknode(NUNOP); n->op = '!'; n->a = atom(); return n; }
  if (tok == TDELETE) {
    next(); Node *target = postfix();
    if (target->kind != NINDEX) { fprintf(stderr, "delete requires obj.key or obj[key]\n"); exit(1); }
    Node *n = mknode(NDELETE); n->a = target->a; n->b = target->b; return n;
  }
  fprintf(stderr, "unexpected token %d\n", tok); exit(1);
}

static Node *postfix(void) {
  Node *n = atom();
  while (tok == '[' || tok == '.') {
    if (tok == '[') { next(); Node *nd = mknode(NINDEX); nd->a = n; nd->b = expr(); expect(']'); n = nd; }
    else { next(); Node *nd = mknode(NINDEX); nd->a = n; nd->b = mknode(NSTR); nd->b->strid = intern(tokname); next(); n = nd; }
  } return n;
}

static Node *binop(Node*(*sub)(void), int *ops, int *toks, int n) {
  Node *left = sub();
  for (;;) {
    int found = -1; for (int i = 0; i < n; i++) if (tok == toks[i]) { found = i; break; }
    if (found < 0) return left; next(); Node *nd = mknode(NBINOP); nd->op = ops[found]; nd->a = left; nd->b = sub(); left = nd;
  }
}

static Node *muldiv(void) { return binop(postfix, (int[]){OMUL,ODIV,OMOD}, (int[]){'*','/','%'}, 3); }
static Node *addsub(void) { return binop(muldiv, (int[]){OADD,OSUB}, (int[]){'+','-'}, 2); }
static Node *cmp(void)    { return binop(addsub, (int[]){OLT,OGT,OLE,OGE}, (int[]){'<','>',TLE,TGE}, 4); }
static Node *expr(void)   { return binop(cmp, (int[]){OEQ,ONE}, (int[]){TEQ,TNE}, 2); }

static Node *stmt(void);
static Node *block(void) {
  Node *n = mknode(NBLOCK);
  if (tok == '{') { next(); while (tok != '}') n->stmts[n->nstmts++] = stmt(); next(); }
  else n->stmts[n->nstmts++] = stmt(); return n;
}

static Node *stmt(void) {
  if (tok == TLET || tok == TCONST) {
    int is_const = tok == TCONST;
    next(); char name[64]; strcpy(name, tokname); next(); expect('=');
    Node *n = mknode(NASSIGN); strcpy(n->name, name); n->bval = 1; n->op = is_const;
    n->a = expr(); expect(';'); return n;
  }
  if (tok == TRET) {
    next(); Node *n = mknode(NRET); if (tok != ';') n->a = expr(); expect(';'); return n;
  }
  if (tok == TFN) {
    next(); int idx = nfuncs++; strcpy(funcs[idx].name, tokname); next(); expect('(');
    if (tok != ')') { strcpy(funcs[idx].params[funcs[idx].nparam++], tokname); next();
      while (tok == ',') { next(); strcpy(funcs[idx].params[funcs[idx].nparam++], tokname); next(); } }
    expect(')'); Node *n = mknode(NFUNC); n->num = idx; n->a = block(); return n;
  }
  if (tok == TIF) {
    next(); expect('('); Node *n = mknode(NIF); n->a = expr(); expect(')');
    n->b = block(); if (tok == TELSE) { next(); n->c = block(); } return n;
  }
  if (tok == TWHILE) {
    next(); expect('('); Node *n = mknode(NWHILE); n->a = expr(); expect(')');
    n->b = block(); return n;
  }
  if (tok == '{') return block();
  if (tok == TID) {
    char name[64]; strcpy(name, tokname); next();
    if (tok == '=') {
      next(); Node *n = mknode(NASSIGN); strcpy(n->name, name);
      n->a = expr(); expect(';'); return n;
    }
    if (tok == '(') {
      next(); Node *n = mknode(NCALL); strcpy(n->name, name);
      if (tok != ')') { n->args[n->nargs++] = expr(); while (tok == ',') { next(); n->args[n->nargs++] = expr(); } }
      expect(')'); expect(';'); return n;
    }
    if (tok == '[' || tok == '.') {
      Node *base = mknode(NID); strcpy(base->name, name);
      while (tok == '[' || tok == '.') {
        Node *nd = mknode(NINDEX); nd->a = base;
        if (tok == '[') { next(); nd->b = expr(); expect(']'); } else { next(); nd->b = mknode(NSTR); nd->b->strid = intern(tokname); next(); }
        base = nd;
      }
      if (tok == '=') { next(); base->kind = NIDX_ASSIGN; base->c = expr(); expect(';'); return base; }
      expect(';'); return base;
    }
    Node *id = mknode(NID); strcpy(id->name, name); expect(';'); return id;
  } Node *n = expr(); expect(';'); return n;
}

static uint64_t code[65536]; static int cp; static uint64_t vars_[256]; static char *varnames[256]; static int nvars, nvars_hwm;
static int varconst[256];
static struct { int ip; uint64_t saved[64]; int base, count; } frames[256]; static int csp;
static int varslot(char *n) { for (int i = nvars-1; i >= 0; i--) if (!strcmp(varnames[i], n)) return i; varnames[nvars] = strdup(n); if (++nvars > nvars_hwm) nvars_hwm = nvars; return nvars-1; }
static int newslot(char *n) { varnames[nvars] = strdup(n); if (++nvars > nvars_hwm) nvars_hwm = nvars; return nvars-1; }

#define EMIT(o)  (code[cp++] = (uint64_t)(o))
#define EMITV(v) (code[cp++] = (v))
#define PATCH(a) (code[a] = (uint64_t)cp)

static void compile(Node *n) {
  switch (n->kind) {
  case NNUM:    EMIT(OPUSH); EMITV(mknum(n->num)); break;
  case NSTR:    EMIT(OPUSH); EMITV(mkstr(n->strid)); break;
  case NBOOL:   EMIT(OPUSH); EMITV(mkbool(n->bval)); break;
  case NNIL:    EMIT(OPUSH); EMITV(mknil()); break;
  case NID:     EMIT(OLOAD); EMIT(varslot(n->name)); break;
  case NASSIGN: { compile(n->a); int slot = n->bval ? newslot(n->name) : varslot(n->name);
    if (!n->bval && varconst[slot]) { fprintf(stderr, "cannot reassign const '%s'\n", n->name); exit(1); }
    if (n->op) varconst[slot] = 1;
    EMIT(OSTORE); EMIT(slot); } break;
  case NBINOP:  compile(n->a); compile(n->b); EMIT(n->op); break;
  case NUNOP:   compile(n->a); EMIT(n->op == '-' ? ONEG : ONOT); break;
  case NCALL: {
    for (int i = 0; i < n->nargs; i++) compile(n->args[i]);
    int id = find_builtin(n->name);
    if (id >= 0) { EMIT(OCALL); EMIT(id); EMIT(n->nargs); }
    else { int fid = find_func(n->name);
      if (fid < 0) { fprintf(stderr, "unknown function '%s'\n", n->name); exit(1); }
      EMIT(OCALL_U); EMIT(fid); EMIT(n->nargs); }
  } break;
  case NFUNC: { int idx = (int)n->num; EMIT(OJMP); int skip = cp; EMIT(0); funcs[idx].addr = cp;
    funcs[idx].sbase = nvars; int old_hwm = nvars_hwm; nvars_hwm = nvars;
    for (int i = 0; i < funcs[idx].nparam; i++) funcs[idx].slots[i] = varslot(funcs[idx].params[i]);
    compile(n->a); funcs[idx].scount = nvars_hwm - funcs[idx].sbase;
    nvars_hwm = nvars_hwm > old_hwm ? nvars_hwm : old_hwm;
    EMIT(OPUSH); EMITV(mknil()); EMIT(ORET); PATCH(skip); } break;
  case NRET: if (n->a) compile(n->a); else { EMIT(OPUSH); EMITV(mknil()); } EMIT(ORET); break;
  case NIF:
    compile(n->a); EMIT(OJEZ); { int p1 = cp; EMIT(0); compile(n->b);
    if (n->c) { EMIT(OJMP); int p2 = cp; EMIT(0); PATCH(p1); compile(n->c); PATCH(p2); } else PATCH(p1); } break;
  case NWHILE: {
    int top = cp; compile(n->a); EMIT(OJEZ); int ex = cp; EMIT(0);
    compile(n->b); EMIT(OJMP); EMIT(top); PATCH(ex); } break;
  case NARR:   for (int i = 0; i < n->nargs; i++) compile(n->args[i]); EMIT(OARR); EMIT(n->nargs); break;
  case NOBJ:   for (int i = 0; i < n->nargs; i++) compile(n->args[i]); EMIT(OOBJ); EMIT(n->nargs/2); break;
  case NINDEX: compile(n->a); compile(n->b); EMIT(OIDX); break;
  case NIDX_ASSIGN: compile(n->a); compile(n->b); compile(n->c); EMIT(OIDX_SET); break;
  case NDELETE: compile(n->a); compile(n->b); EMIT(ODEL); break;
  case NBLOCK: { int saved = nvars;
    for (int i = 0; i < n->nstmts; i++) { compile(n->stmts[i]); int k = n->stmts[i]->kind; if (k==NCALL||k==NID||k==NNUM||k==NSTR||k==NBOOL||k==NNIL||k==NBINOP||k==NUNOP||k==NINDEX||k==NDELETE) EMIT(OPOP); }
    nvars = saved; } break;
  }
}

static void run(void) {
  static void *dispatch[] = {
    [OPUSH]=&&L_PUSH, [OLOAD]=&&L_LOAD, [OSTORE]=&&L_STORE,
    [OADD]=&&L_ADD, [OSUB]=&&L_SUB, [OMUL]=&&L_MUL, [ODIV]=&&L_DIV, [OMOD]=&&L_MOD,
    [OEQ]=&&L_EQ, [ONE]=&&L_NE, [OLT]=&&L_LT, [OGT]=&&L_GT, [OLE]=&&L_LE, [OGE]=&&L_GE,
    [OJMP]=&&L_JMP, [OJNZ]=&&L_JNZ, [OJEZ]=&&L_JEZ,
    [OHALT]=&&L_HALT, [ONEG]=&&L_NEG, [ONOT]=&&L_NOT, [OCALL]=&&L_CALL,
    [OCALL_U]=&&L_CALL_U, [ORET]=&&L_RET, [OPOP]=&&L_POP,
    [OARR]=&&L_ARR, [OOBJ]=&&L_OBJ, [OIDX]=&&L_IDX, [OIDX_SET]=&&L_IDX_SET, [ODEL]=&&L_DEL,
  };
  uint64_t stack[4096]; int sp = 0, ip = 0;
  #define NEXT goto *dispatch[code[ip++]]
  #define POP  stack[--sp]
  #define PUSH(v) (stack[sp++]=(v))
  #define NUMBIN(op) { double b=asnum(POP),a=asnum(POP); PUSH(mknum(a op b)); NEXT; }
  #define CMPBIN(op) { double b=asnum(POP),a=asnum(POP); PUSH(mkbool(a op b)); NEXT; }
  NEXT;
  L_PUSH:  PUSH(code[ip++]); NEXT;
  L_LOAD:  PUSH(vars_[code[ip++]]); NEXT;
  L_STORE: vars_[code[ip++]] = POP; NEXT;
  L_ADD: {
    uint64_t bv = POP, av = POP;
    if (vtag(av)==T_STR && vtag(bv)==T_STR) {
      char buf[2048]; snprintf(buf, sizeof buf, "%s%s", strtab[vdata(av)], strtab[vdata(bv)]);
      PUSH(mkstr(intern(buf)));
    } else PUSH(mknum(asnum(av)+asnum(bv))); NEXT;
  }
  L_SUB: NUMBIN(-)  L_MUL: NUMBIN(*)  L_DIV: NUMBIN(/)
  L_MOD: { double b=asnum(POP),a=asnum(POP); PUSH(mknum(fmod(a,b))); NEXT; }
  L_EQ: { uint64_t b=POP,a=POP; 
    if(vtag(a)==T_STR&&vtag(b)==T_STR) PUSH(mkbool(!strcmp(strtab[vdata(a)],strtab[vdata(b)])));
    else if(vtag(a)==vtag(b)) PUSH(mkbool(a==b)); else PUSH(mkbool(0)); NEXT; }
  L_NE: { uint64_t b=POP,a=POP; 
    if(vtag(a)==T_STR&&vtag(b)==T_STR) PUSH(mkbool(!!strcmp(strtab[vdata(a)],strtab[vdata(b)])));
    else if(vtag(a)==vtag(b)) PUSH(mkbool(a!=b)); else PUSH(mkbool(1)); NEXT; }
  L_LT:  CMPBIN(<)  L_GT:  CMPBIN(>)
  L_LE:  CMPBIN(<=) L_GE:  CMPBIN(>=)
  L_NEG: stack[sp-1] = mknum(-asnum(stack[sp-1])); NEXT;
  L_NOT: stack[sp-1] = mkbool(isfalsy(stack[sp-1])); NEXT;
  L_JMP: ip = (int)code[ip]; NEXT;
  L_JNZ: ip = !isfalsy(POP) ? (int)code[ip] : ip+1; NEXT;
  L_JEZ: ip = isfalsy(POP) ? (int)code[ip] : ip+1; NEXT;
  L_CALL: {
    int id = (int)code[ip++], nargs = (int)code[ip++];
    uint64_t args[16]; for (int i = nargs-1; i >= 0; i--) args[i] = POP;
    PUSH(builtins[id].fn(args, nargs)); NEXT;
  }
  L_CALL_U: { int idx = (int)code[ip++], nargs = (int)code[ip++];
    uint64_t args[16]; for (int i = nargs-1; i >= 0; i--) args[i] = POP;
    frames[csp].ip = ip; frames[csp].base = funcs[idx].sbase; frames[csp].count = funcs[idx].scount;
    for (int i = 0; i < funcs[idx].scount; i++) frames[csp].saved[i] = vars_[funcs[idx].sbase + i];
    csp++; for (int i = 0; i < nargs; i++) vars_[funcs[idx].slots[i]] = args[i];
    ip = funcs[idx].addr; NEXT; }
  L_RET: { uint64_t rv = POP; --csp;
    for (int i = 0; i < frames[csp].count; i++) vars_[frames[csp].base + i] = frames[csp].saved[i];
    ip = frames[csp].ip; PUSH(rv); NEXT; }
  L_POP: sp--; NEXT;
  L_ARR: { int n = (int)code[ip++]; int id = new_arr();
    for (int i = n; i > 0; i--) arr_push(&arrs[id], stack[sp-i]);
    sp -= n; PUSH(mkarr(id)); NEXT; }
  L_OBJ: { int n = (int)code[ip++]; int id = new_obj();
    for (int i = n; i > 0; i--) { uint64_t v = stack[sp-i*2+1]; uint64_t k = stack[sp-i*2]; obj_set(&objs[id], vdata(k), v); }
    sp -= n*2; PUSH(mkobj(id)); NEXT; }
  L_IDX: { uint64_t key = POP, cont = POP;
    if (vtag(cont)==T_ARR) { int idx = (int)asnum(key); Arr *a = &arrs[vdata(cont)]; PUSH(idx>=0 && idx<a->len ? a->data[idx] : mknil()); }
    else if (vtag(cont)==T_OBJ) { int k = vtag(key)==T_STR ? vdata(key) : intern((char*)""); PUSH(obj_get(&objs[vdata(cont)], k)); }
    else PUSH(mknil()); NEXT; }
  L_IDX_SET: { uint64_t val = POP, key = POP, cont = POP;
    if (vtag(cont)==T_ARR) { int idx = (int)asnum(key); Arr *a = &arrs[vdata(cont)]; if (idx>=0 && idx<a->len) a->data[idx] = val; }
    else if (vtag(cont)==T_OBJ) { int k = vtag(key)==T_STR ? vdata(key) : intern((char*)""); obj_set(&objs[vdata(cont)], k, val); }
    PUSH(val); NEXT; }
  L_DEL: { uint64_t key = POP, cont = POP;
    if (vtag(cont)==T_OBJ) { Obj *o = &objs[vdata(cont)]; int k = vtag(key)==T_STR ? vdata(key) : intern((char*)"");
      int found = 0; for (int i = 0; i < o->len; i++) if (o->keys[i] == k) { o->keys[i] = o->keys[o->len-1]; o->vals[i] = o->vals[o->len-1]; o->len--; found = 1; break; }
      PUSH(mkbool(found)); } else PUSH(mkbool(0)); NEXT; }
  L_HALT: return;
}

int main(int argc, char **argv) {
  FILE *f = argc > 1 ? fopen(argv[1], "r"): stdin; if (!f) { perror("fopen"); return 1; }
  int len = fread(src, 1, sizeof(src)-1, f); src[len] = 0; if (f != stdin) fclose(f);
  p = src; next(); Node *prog = mknode(NBLOCK); while (tok != TEOF) prog->stmts[prog->nstmts++] = stmt();
  compile(prog); EMIT(OHALT); run();
}