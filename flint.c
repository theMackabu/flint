#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>

#define PREFIX    0xFFF0000000000000ULL
#define DATA_MASK 0x00007FFFFFFFFFFFULL
enum Type { T_NUM, T_STR, T_BOOL, T_NIL };

static inline uint64_t mknum(double d)           { uint64_t v; memcpy(&v, &d, 8); return v; }
static inline double asnum(uint64_t v)           { double d; memcpy(&d, &v, 8); return d; }
static inline uint64_t mktag(int t, uint64_t d)  { return PREFIX | ((uint64_t)t << 47) | (d & DATA_MASK); }
static inline int  vtag(uint64_t v)              { return v > PREFIX ? (v >> 47) & 0x1F : T_NUM; }
static inline uint64_t vdata(uint64_t v)         { return v & DATA_MASK; }
static inline uint64_t mkstr(uint64_t id)        { return mktag(T_STR, id); }
static inline uint64_t mkbool(int b)             { return mktag(T_BOOL, b); }
static inline uint64_t mknil(void)               { return mktag(T_NIL, 0); }
static inline int isfalsy(uint64_t v)            { return vtag(v)==T_NIL || (vtag(v)==T_BOOL && !vdata(v)) || (vtag(v)==T_NUM && asnum(v)==0); }

static char *strtab[4096]; static int nstrs; static int intern(char *s) {
  for (int i = 0; i < nstrs; i++) if (!strcmp(strtab[i], s)) return i;
  strtab[nstrs] = strdup(s); return nstrs++;
}

static uint64_t b_print(uint64_t *args, int n) {
  for (int i = 0; i < n; i++) { if (i) printf(" "); switch (vtag(args[i])) {
  case T_NUM:  { double d = asnum(args[i]); d == (int)d ? printf("%d",(int)d) : printf("%g",d); } break;
  case T_STR:  printf("%s", strtab[vdata(args[i])]); break;
  case T_BOOL: printf("%s", vdata(args[i]) ? "true" : "false"); break; case T_NIL:  printf("nil"); break;
  }} return mknil();
}

static uint64_t b_println(uint64_t *args, int n) { b_print(args, n); printf("\n"); return mknil(); }
static uint64_t b_len(uint64_t *args, int n) { (void)n; return vtag(args[0]) == T_STR ? mknum(strlen(strtab[vdata(args[0])])) : mknum(0); }

static uint64_t b_type(uint64_t *args, int n) {
  (void)n; const char *names[] = {"num","str","bool","nil"};
  return mkstr(intern((char*)names[vtag(args[0])]));
}

static uint64_t b_str(uint64_t *args, int n) {
  (void)n; char buf[64]; switch (vtag(args[0])) {
  case T_NUM: { double d=asnum(args[0]); d==(int)d ? snprintf(buf,64,"%d",(int)d) : snprintf(buf,64,"%g",d); break; }
  case T_STR: return args[0]; case T_BOOL: return mkstr(intern(vdata(args[0]) ? "true" : "false")); case T_NIL:  return mkstr(intern("nil"));
  } return mkstr(intern(buf));
}

static uint64_t b_num(uint64_t *args, int n) {
  (void)n; if (vtag(args[0]) == T_STR) return mknum(strtod(strtab[vdata(args[0])], NULL));
  if (vtag(args[0]) == T_BOOL) return mknum(vdata(args[0]));
  return args[0];
}

typedef uint64_t (*Builtin)(uint64_t*, int);
static struct { char *name; Builtin fn; } builtins[] = {
  {"print", b_print}, {"println", b_println}, {"len", b_len}, {"type", b_type}, {"str", b_str}, {"num", b_num},
};

#define NBUILTINS (sizeof builtins / sizeof builtins[0])
static int find_builtin(char *name) { for (int i = 0; i < (int)NBUILTINS; i++) if (!strcmp(builtins[i].name, name)) return i; return -1; }

static struct { char name[64]; char params[16][64]; int nparam; int addr; int slots[16]; } funcs[256];
static int nfuncs; static int find_func(char *name) { for (int i = 0; i < nfuncs; i++) if (!strcmp(funcs[i].name, name)) return i; return -1; }

enum Tok { TNUM=128, TSTR, TID, TIF, TELSE, TWHILE, TTRUE, TFALSE, TNIL, TEQ, TNE, TLE, TGE, TFN, TRET, TLET, TEOF };
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
    else if (!strcmp(tokname,"true")) tok=TTRUE; else if (!strcmp(tokname,"false")) tok=TFALSE;
    else if (!strcmp(tokname,"nil")) tok=TNIL; return;
  }
  char c = *p++;
  if (c=='=' && *p=='=') { p++; tok=TEQ; } else if (c=='!' && *p=='=') { p++; tok=TNE; }
  else if (c=='<' && *p=='=') { p++; tok=TLE; } else if (c=='>' && *p=='=') { p++; tok=TGE; } else tok = c;
}

enum Op { OPUSH, OLOAD, OSTORE, OADD, OSUB, OMUL, ODIV, OMOD, OEQ, ONE, OLT, OGT, OLE, OGE, OJMP, OJNZ, OJEZ, OHALT, ONEG, ONOT, OCALL, OCALL_U, ORET, OPOP };
enum Nd { NNUM, NSTR, NBOOL, NNIL, NID, NBINOP, NUNOP, NASSIGN, NIF, NWHILE, NCALL, NBLOCK, NFUNC, NRET };

typedef struct Node {
  int kind, op; double num; int strid, bval; char name[64];
  struct Node *a, *b, *c; struct Node *args[16]; int nargs; struct Node *stmts[256]; int nstmts;
} Node;

static void expect(int t)  { if (tok != t) { fprintf(stderr, "expected '%c' got %d\n", t, tok); exit(1); } next(); }
static Node *mknode(int k) { Node *n = calloc(1, sizeof(Node)); n->kind = k; return n; }
static Node *expr(void);

static Node *atom(void) {
  if (tok == TNUM)   { Node *n = mknode(NNUM); n->num = toknum; next(); return n; }
  if (tok == TSTR)   { Node *n = mknode(NSTR); n->strid = intern(tokstr); next(); return n; }
  if (tok == TTRUE)  { Node *n = mknode(NBOOL); n->bval = 1; next(); return n; }
  if (tok == TFALSE) { Node *n = mknode(NBOOL); n->bval = 0; next(); return n; }
  if (tok == TNIL)   { Node *n = mknode(NNIL); next(); return n; }
  if (tok == TID) {
    char name[64]; strcpy(name, tokname); next(); if (tok == '(') {
      next(); Node *n = mknode(NCALL); strcpy(n->name, name);
      if (tok != ')') { n->args[n->nargs++] = expr(); while (tok == ',') { next(); n->args[n->nargs++] = expr(); } }
      expect(')'); return n;
    } Node *n = mknode(NID); strcpy(n->name, name); return n;
  }
  if (tok == '(') { next(); Node *n = expr(); expect(')'); return n; }
  if (tok == '-') { next(); Node *n = mknode(NUNOP); n->op = '-'; n->a = atom(); return n; }
  if (tok == '!') { next(); Node *n = mknode(NUNOP); n->op = '!'; n->a = atom(); return n; }
  fprintf(stderr, "unexpected token %d\n", tok); exit(1);
}

static Node *binop(Node*(*sub)(void), int *ops, int *toks, int n) {
  Node *left = sub();
  for (;;) {
    int found = -1; for (int i = 0; i < n; i++) if (tok == toks[i]) { found = i; break; }
    if (found < 0) return left; next(); Node *nd = mknode(NBINOP); nd->op = ops[found]; nd->a = left; nd->b = sub(); left = nd;
  }
}

static Node *muldiv(void) { return binop(atom, (int[]){OMUL,ODIV,OMOD}, (int[]){'*','/','%'}, 3); }
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
  if (tok == TLET) {
    next(); char name[64]; strcpy(name, tokname); next(); expect('=');
    Node *n = mknode(NASSIGN); strcpy(n->name, name); n->bval = 1; n->a = expr(); expect(';'); return n;
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
    } Node *id = mknode(NID); strcpy(id->name, name); expect(';'); return id;
  } Node *n = expr(); expect(';'); return n;
}

static uint64_t code[65536]; static int cp; static uint64_t vars_[256]; static char *varnames[256]; static int nvars;
static struct { int ip; uint64_t saved[16]; int nslots; int slots[16]; } frames[256]; static int csp;
static int varslot(char *n) { for (int i = nvars-1; i >= 0; i--) if (!strcmp(varnames[i], n)) return i; varnames[nvars] = strdup(n); return nvars++; }
static int newslot(char *n) { varnames[nvars] = strdup(n); return nvars++; }

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
  case NASSIGN: compile(n->a); EMIT(OSTORE); EMIT(n->bval ? newslot(n->name) : varslot(n->name)); break;
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
    for (int i = 0; i < funcs[idx].nparam; i++) funcs[idx].slots[i] = varslot(funcs[idx].params[i]);
    compile(n->a); EMIT(OPUSH); EMITV(mknil()); EMIT(ORET); PATCH(skip); } break;
  case NRET: if (n->a) compile(n->a); else { EMIT(OPUSH); EMITV(mknil()); } EMIT(ORET); break;
  case NIF:
    compile(n->a); EMIT(OJEZ); { int p1 = cp; EMIT(0); compile(n->b);
    if (n->c) { EMIT(OJMP); int p2 = cp; EMIT(0); PATCH(p1); compile(n->c); PATCH(p2); } else PATCH(p1); } break;
  case NWHILE: {
    int top = cp; compile(n->a); EMIT(OJEZ); int ex = cp; EMIT(0);
    compile(n->b); EMIT(OJMP); EMIT(top); PATCH(ex); } break;
  case NBLOCK: { int saved = nvars;
    for (int i = 0; i < n->nstmts; i++) { compile(n->stmts[i]); int k = n->stmts[i]->kind; if (k==NCALL||k==NID||k==NNUM||k==NSTR||k==NBOOL||k==NNIL||k==NBINOP||k==NUNOP) EMIT(OPOP); }
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
  L_EQ:  CMPBIN(==) L_NE:  CMPBIN(!=)
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
    frames[csp].ip = ip; frames[csp].nslots = funcs[idx].nparam;
    for (int i = 0; i < funcs[idx].nparam; i++) { frames[csp].slots[i] = funcs[idx].slots[i]; frames[csp].saved[i] = vars_[funcs[idx].slots[i]]; }
    csp++; for (int i = 0; i < nargs; i++) vars_[funcs[idx].slots[i]] = args[i];
    ip = funcs[idx].addr; NEXT; }
  L_RET: { uint64_t rv = POP; --csp;
    for (int i = 0; i < frames[csp].nslots; i++) vars_[frames[csp].slots[i]] = frames[csp].saved[i];
    ip = frames[csp].ip; PUSH(rv); NEXT; }
  L_POP: sp--; NEXT;
  L_HALT: return;
}

int main(int argc, char **argv) {
  FILE *f = argc > 1 ? fopen(argv[1], "r"): stdin; if (!f) { perror("fopen"); return 1; }
  int len = fread(src, 1, sizeof(src)-1, f); src[len] = 0; if (f != stdin) fclose(f);
  p = src; next(); Node *prog = mknode(NBLOCK); while (tok != TEOF) prog->stmts[prog->nstmts++] = stmt();
  compile(prog); EMIT(OHALT); run();
}