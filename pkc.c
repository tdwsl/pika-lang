#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define BUFSZ 200
#define EXBUFSZ 1024
#define NAMEBUFSZ 1024*768
#define MAXFUNCTIONS 400
#define MAXARGS 30
#define MAXVARS 50
#define DATASZ 1024*512
#define MEMORYSZ 1024*512
#define MAXLISTS 250
#define ORG 0x10000
#define CSTACKSZ 128
#define MAXEXITS 95

#define dbprintf(...) if(debug) printf(__VA_ARGS__)

struct function {
    char *s;
    char *args[MAXARGS];
    char *vars[MAXVARS];
    int varp[MAXVARS];
    int varn[MAXVARS];
    int nargs, nvars, addr;
};

enum {
    INS_SYS=0, INS_PUSHBPP, INS_POPBPV, INS_PUSHBPV,
    INS_JZ, INS_JNZ, INS_JMP, INS_ADDSP,
    INS_PUSH, INS_GLOBAL, INS_JAL,

    INS_POPRA=0x10, INS_PUSHRA, INS_POPRV, INS_PUSHRV,
    INS_POPBP, INS_PUSHBP, INS_MOVBPSP, INS_MOVSPBP,
    INS_JRA, INS_POPGP,

    INS_INC=0x20, INS_DEC, INS_INV, INS_NEG,
    INS_LB, INS_LH, INS_LW,
    INS_SB=0x28, INS_SH, INS_SW,

    INS_ADD=0x30, INS_SUB, INS_ADD4, INS_AND,
    INS_OR, INS_XOR, INS_SHL, INS_SHR,
    INS_EQ, INS_NE, INS_LT, INS_LTE,
    INS_DIV, INS_MUL, INS_MOD,
};

const char *insStrs[] = {
    "SYS", "PUSHBPP", "POPBPV", "PUSHBPV",
    "JZ", "JNZ", "JMP", "ADDSP",
    "PUSH", "GLOBAL", "JAL", 0,
    0,0,0,0,
    "POPRA", "PUSHRA", "POPRV", "PUSHRV",
    "POPBP", "PUSHBP", "MOVBPSP", "MOVSPBP",
    "JRA", "POPGP", 0, 0,
    0,0,0,0,
    "INC", "DEC", "INV", "NEG",
    "LB", "LH", "LW", 0,
    "SB", "SH", "SW", 0,
    0,0,0,0,
    "ADD", "SUB", "ADD4", "AND",
    "OR", "XOR", "SHL", "SHR",
    "EQ", "NE", "LT", "LTE",
    "DIV", "MUL", "MOD", 0,
};

const char *ops[] = {
    ":", ".", "NOT", "NEG",
    "*", "/", "DIV", "MOD", "AND",
    "+", "-", "OR", "XOR",
    "=", "<>", "<", "<=", ">", ">=", // "IN",
    0,
};

const char **litOps = ops+3;

const char *opsIns[] = {
    "add4\nlw", "add\nlb", "inv", "neg",
    "mul", "div", "div", "mod", "and",
    "add", "sub", "or", "xor",
    "eq", "ne", "lt", "lte", "lte\ninv", "lt\ninv", // "call in",
};

const int opOps[] = {
    INS_ADD4|INS_LW<<8, INS_ADD|INS_LB<<8, INS_INV, INS_NEG,
    INS_MUL, INS_DIV, INS_DIV, INS_MOD, INS_AND,
    INS_ADD, INS_SUB, INS_OR, INS_XOR,
    INS_EQ, INS_NE, INS_LT, INS_LTE, INS_LTE|INS_INV<<8, INS_LT|INS_INV<<8, 0,
};

const char *delim = ",.:;()[]{}+-*/<>=@\"'";
const char *delinkable = "+-*/.:<>";

FILE *fp;
struct function functions[MAXFUNCTIONS];
int nfunctions = 0;
char nameBuf[NAMEBUFSZ];
char *namep = nameBuf;
int nlabels = 0;
int scope = 0;
unsigned char data[DATASZ];
unsigned char memory[MEMORYSZ];
unsigned int ndata = 0, nmemory = 0;
struct function *lastFunction = 0;
char *last;
int lists[MAXLISTS];
int nlists = 0, listi = 0;
int lineNo = 1;
int cstack[CSTACKSZ];
int csp = 0;
const char *filename = 0;
unsigned char debug = 0;
int exits[MAXEXITS];
int nexits = 0;

char nextc() {
    char c;
    c = fgetc(fp);
    if(c == '\n') lineNo++;
    return c;
}

void prevc() {
    fseek(fp, -1, SEEK_CUR);
    if(nextc() == '\n') lineNo--;
    fseek(fp, -1, SEEK_CUR);
}

void _getNext(char *buf) {
    char *p;

    p = buf;
    while(!feof(fp)) {
        *p = nextc();
        if(*p <= 32) {
            if(p != buf) break;
        } else if(strchr(delim, *p)) {
            if(p == buf) p++;
            else prevc();
            break;
        } else {
            if(*p >= 'a' && *p <= 'z') *p += 'A'-'a';
            p++;
        }
    }
    *p = 0;
}

void errstart() {
    printf("%s:%d: error: ", filename, lineNo);
}

void error(const char *err) {
    errstart();
    printf("%s\n", err);
    exit(1);
}

char backChar(char c) {
    switch(c) {
    case '"': case '\'': case '\\': return c;
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    default:
        errstart();
        printf("invalid char '\\%c'\n", c);
        exit(1);
    }
}

void getNext(char *buf);

void comment1(char *buf) {
    while(!feof(fp)) {
        if(nextc() == '*' ) {
            if(nextc() == ')') {
                getNext(buf);
                return;
            } else prevc();
        }
    }
    error("unterminated (* comment");
}

void comment2(char *buf) {
    while(!feof(fp)) {
        if(nextc() == '}') { getNext(buf); return; }
    }
    error("unterminated { comment\n");
}

void comment3(char *buf) {
    while(!feof(fp)) {
        if(nextc() == '\n') { getNext(buf); return; }
    }
}

void getNext(char *buf) {
    char *p;
    char c;

    _getNext(buf);
    if(strlen(buf) != 1) return;

    if(*buf == '"') {
        p = buf+1;
        for(;;) {
            if(feof(fp)) error("unterminated quote");
            *p = nextc();
            if(*p == '"') break;
            if(*p == '\n') error("unterminated quote");
            if(*p == '\\') *p = backChar(nextc());
            p++;
        }
        *p = 0;
    } else if(*buf == '\'') {
        p = buf+1;
        *p = nextc();
        if(*p == '\n') error("unterminated single quote");
        if(*p == '\\') *p = backChar(nextc());
        if(nextc() != '\'') error("unterminated single quote");
        *(++p) = 0;
    } else if(*buf == '{') {
        comment2(buf);
    } else if(*buf == '(') {
        if(nextc() == '*') comment1(buf);
        else prevc();
    } else if(strchr(delinkable, *buf)) {
        c = nextc();
        if(*buf == '/' && c == '/') { comment3(buf); return; }
        else if(*buf == '<' && c == '>');
        else if(c == '=');
        else { prevc(); return; }
        buf[1] = c;
        buf[2] = 0;
    }
}

void expect(char *buf, const char *s) {
    if(strcmp(s, buf)) {
        errstart();
        printf("expected %s, got %s\n", s, buf);
        exit(1);
    }
}

int expect1(char *buf, const char *s, const char *s1) {
    if(s == s1) expect(buf, s);
    else if(strcmp(s, buf) && strcmp(s1, buf)) {
        errstart();
        printf("expected %s or %s, got %s\n", s, s1, buf);
        exit(1);
    }
    return !strcmp(s1, buf);
}

void next(const char *s) {
    char buf[BUFSZ];
    getNext(buf);
    expect(buf, s);
}

int strindex(const char **strs, char *s) {
    int i;
    for(i = 0; strs[i]; i++) if(!strcmp(strs[i], s)) return i;
    return -1;
}

void pushRpn(char **ex, int nex, char **rp, int *nrp, int i) {
    if(i < 0 || i >= nex || !ex[i]) return;
    rp[(*nrp)++] = ex[i];
    ex[i] = 0;
}

void rpnSweep(char **ex, int nex, char **rp, int *nrp, const char *op) {
    int i;
    for(i = 0; i < nex; i++) {
        if(ex[i] && !strcmp(ex[i], op)) {
            pushRpn(ex, nex, rp, nrp, i-1);
            pushRpn(ex, nex, rp, nrp, i+1);
            rp[(*nrp)++] = ex[i];
            ex[i++] = 0;
        }
    }
}

void toRpn(char **ex, int nex, char **rp, int *nrp, char ***bp, int nbp) {
    int i, j, l, bpi;
    struct function *f;

    //for(i = 0; i < nex; i++) printf("[%s]", ex[i]);

    bpi = 0;

    if(nex == 1) {
        if(strindex(ops, ex[0]) != -1)
            error("expected value(s) with operator");
        rp[(*nrp)++] = ex[0];
        ex[0] = 0;
    } else {
        rpnSweep(ex, nex, rp, nrp, ":");
        rpnSweep(ex, nex, rp, nrp, ".");
        for(i = 0; i < nex; i++) {
            if(ex[i] && (!strcmp(ex[i], "NOT") || !strcmp(ex[i], "NEG"))) {
                pushRpn(ex, nex, rp, nrp, i+1);
                rp[(*nrp)++] = ex[i];
                ex[i++] = 0;
            }
        }
        for(i = 3; ops[i]; i++)
            rpnSweep(ex, nex, rp, nrp, ops[i]);
        for(i = 0; i < nex; i++)
            if(ex[i]) error("invalid expression");
    }

    for(i = 0; i < *nrp; i++) {
        if(!strcmp(rp[i], "(")) {
            for(l = 0; bp[bpi][l]; l++);
            for(j = *nrp-1; j >= i; j--)
                rp[j+l-1] = rp[j];
            for(j = 0; j < l; j++)
                rp[i+j] = bp[bpi][j];
            bpi++;
            *nrp += l-1;
            i += l;
        }
    }
}

struct function *findFunction(char *s) {
    int i;
    for(i = 0; i < nfunctions; i++)
        if(!strcmp(functions[i].s, s)) return &functions[i];
    return 0;
}

int functionStack(struct function *f) {
    int t, i;
    t = 0;
    for(i = 0; i < f->nvars; i++)
        t += (f->varn[i]) ? f->varn[i] : 4;
    return t;
}

void addString(char *s) {
    *(uint32_t*)&memory[nmemory] = ndata;
    nmemory += 4;
    strcpy(&data[ndata], s);
    ndata += strlen(s)+1;
}

int value(const char *end, const char *end1, int *n);

void addList() {
    int n, r;
    for(;;) {
        r = value(",", "]", &n);
        *(uint32_t*)&data[ndata] = n;
        ndata += 4;
        if(r) break;
    }
}

int _expression(const char *end, const char *end1, char **rp, int *nrp,
  char **buf) {
    const char *endings[] = {
        ",", ";", ":=", ")", "]", "TO", "THEN", "DO", "ELSE", 0,
    };
    char *ex[50];
    char *br[150];
    char **bp[50];
    char *p;
    int nex, nbr, nbp, n, r, rr, i, j;
    struct function *f;
    char *last = 0;

    if(!end1) end1 = end;

    p = *buf;
    nex = 0;
    nbr = 0;
    nbp = 0;
    f = 0;

    for(;;) {
        getNext(p);
        if(strindex(endings, p) != -1) {
            expect1(p, end, end1);
            r = !strcmp(p, end1);
            break;
        }
        if(!(*p)) expect("<EOF>", end);
        ex[nex++] = p;
        p += strlen(p)+1;
        if(!strcmp(ex[nex-1], "(")) {
            if(f && f->nargs) {
                ex[--nex-1] = "(";
                bp[nbp++] = &br[nbr];
                do {
                    n = 0;
                    rr = _expression(")", ",", &br[nbr], &n, &p);
                    nbr += n;
                } while(rr);
                br[nbr++] = f->s;
                br[nbr++] = 0;
            } else {
                bp[nbp++] = &br[nbr];
                n = 0;
                _expression(")", 0, &br[nbr], &n, &p);
                nbr += n;
                br[nbr++] = 0;
            }
            f = 0;
        } else if((f = findFunction(ex[nex-1])) == lastFunction)
            ex[nex-1] = "**";
        else if(!strcmp(ex[nex-1], "[")) {
            lists[nlists++] = ndata;
            addList();
        } else if(!strcmp(ex[nex-1], "-")) {
            if(nex == 1 || strindex(ops, ex[nex-2]) != -1)
                ex[nex-1] = "NEG";
        }
    }
    *buf = p;

    if(nex == 0)
        error("expected expression");

    if(nex == 1 && strindex(ops, ex[0]) != -1)
        error("expected expression, got operator");

    toRpn(ex, nex, rp, nrp, bp, nbp);

    return r;
}

int hex(char *s, int *n) {
    *n = 0;
    if(!(*s)) return 0;
    while(*s) {
        *n *= 16;
        if(*s >= '0' && *s <= '9') *n += *s - '0';
        else if(*s >= 'A' && *s <= 'F') *n += *s - 'A' + 10;
        else return 0;
        s++;
    }
    return 1;
}

int number(char *s, int *n) {
    *n = 0;
    if(!(*s)) return 0;
    if(s[0] == '0' && s[1] == 'X') return hex(s+2, n);
    if(*s == '$') return hex(s+1, n);
    while(*s) {
        *n *= 10;
        if(*s >= '0' && *s <= '9') *n += *s - '0';
        else return 0;
        s++;
    }
    return 1;
}

void push(char *s) {
    int i;
    struct function *f;
    if(number(s, &i)) {
        dbprintf("push #%d\n", i);
        memory[nmemory++] = INS_PUSH;
        *(uint32_t*)&memory[nmemory] = i;
        nmemory += 4;
    } else if(*s == '"') {
        dbprintf("global #%d\n", ndata);
        memory[nmemory++] = INS_GLOBAL;
        addString(s+1);
    } else if(*s == '\'') {
        dbprintf("push #%d\n", s[1]);
        memory[nmemory++] = INS_PUSH;
        *(uint32_t*)&memory[nmemory] = s[1];
        nmemory += 4;
    } else if(f = findFunction(s)) {
        dbprintf("jal #%s\n", f->s);
        memory[nmemory++] = INS_JAL;
        *(uint32_t*)&memory[nmemory] = f-functions;
        nmemory += 4;
        memory[nmemory++] = INS_PUSHRV;
    } else if(!strcmp(s, "**")) {
        memory[nmemory++] = INS_PUSHRV;
    } else if(!strcmp(s, "[") && listi < nlists) {
        memory[nmemory++] = INS_GLOBAL;
        *(uint32_t*)&memory[nmemory] = lists[listi++];
        nmemory += 4;
    } else {
        f = lastFunction;
        if(!strcmp(f->s, s)) {
            dbprintf("pushrv\n");
            memory[nmemory++] = INS_PUSHRV;
            return;
        }
        for(i = 0; i < f->nargs; i++)
            if(!strcmp(f->args[i], s)) {
                dbprintf("pushbpv !%d\n", (f->nargs+1-i)*4);
                memory[nmemory++] = INS_PUSHBPV;
                *(int16_t*)&memory[nmemory] = (f->nargs+1-i)*4;
                nmemory += 2;
                return;
            }
        for(i = 0; i < f->nvars; i++)
            if(!strcmp(f->vars[i], s)) {
                if(f->varn[i]) {
                    dbprintf("pushbpp !%d\n", f->varp[i]);
                    memory[nmemory++] = INS_PUSHBPP;
                    *(int16_t*)&memory[nmemory] = f->varp[i]+4-f->varn[i];
                    nmemory += 2;
                } else {
                    dbprintf("pushbpv !%d\n", f->varp[i]);
                    memory[nmemory++] = INS_PUSHBPV;
                    *(int16_t*)&memory[nmemory] = f->varp[i];
                    nmemory += 2;
                }
                return;
            }
        errstart();
        printf("unknown value %s\n", s);
        exit(1);
    }
}

void pop(char *s) {
    int i;
    struct function *f;
    if((f = findFunction(s)) && f != lastFunction)
        error("cant assign function");
    f = lastFunction;
    if(!strcmp(f->s, s)) {
        dbprintf("poprv\n");
        memory[nmemory++] = INS_POPRV;
        return;
    }
    for(i = 0; i < f->nargs; i++)
        if(!strcmp(f->args[i], s)) {
            dbprintf("popbpv !%d\n", (f->nargs+1-i)*4);
            memory[nmemory++] = INS_POPBPV;
            *(int16_t*)&memory[nmemory] = (f->nargs+1-i)*4;
            nmemory += 2;
            return;
        }
    for(i = 0; i < f->nvars; i++)
        if(!strcmp(f->vars[i], s)) {
            dbprintf("popbpv !%d\n", f->varp[i]);
            memory[nmemory++] = INS_POPBPV;
            *(int16_t*)&memory[nmemory] = f->varp[i];
            nmemory += 2;
            return;
        }
    errstart();
    dbprintf("unknown value %s\n", s);
    exit(1);
}

int _literal(char *s, int *n) {
    if(number(s, n)) return 1;
    return 0;
}

int literal(char *s) {
    int n;
    if(!_literal(s, &n))
        error("failed to evaluate literal");
    return n;
}

int doOp(int a, int b, int o, int *sp) {
    switch(o) {
    case INS_INV: (*sp)++; return ~a;
    case INS_NEG: (*sp)++; return a*-1;
    case INS_AND: return a&b;
    case INS_OR: return a|b;
    case INS_XOR: return a^b;
    case INS_ADD: return a+b;
    case INS_SUB: return a-b;
    case INS_DIV: return a/b;
    case INS_MOD: return a%b;
    case INS_MUL: return a*b;
    case INS_EQ: return a==b;
    case INS_NE: return a!=b;
    case INS_LT: return a<b;
    case INS_LTE: return a<=b;
    case INS_LTE|INS_INV<<8: return a>b;
    case INS_LT|INS_INV<<8: return a>=b;
    case INS_ADD4|INS_LW<<8: return *(uint32_t*)&memory[a+b<<2];
    case INS_ADD|INS_LB<<8: return memory[a+b];
    default:
        error("invalid operation");
    }
}

void delRpn(char **rp, int *rpn, int n) {
    int i;
    (*rpn) -= n;
    for(i = 0; i < *rpn; i++)
        rp[i] = rp[i+n];
}

int expression(const char *end, const char *end1) {
    char buf[EXBUFSZ];
    char *p;
    char *rp[100];
    int nrp, r, i, j, n;
    nrp = 0;
    struct function *f;

    p = buf;
    r = _expression(end, end1, rp, &nrp, &p);

    for(;;) {
        if(nrp < 2) break;
        if(!_literal(rp[0], &i)) break;
        if(!strcmp(rp[1], "NOT")) {
            delRpn(rp, &nrp, 1);
            rp[0] = p;
            sprintf(p, "%u", ~i);
            p += strlen(p)+1;
        } else if(!strcmp(rp[1], "NEG")) {
            delRpn(rp, &nrp, 1);
            rp[0] = p;
            sprintf(p, "%u", i*-1);
            p += strlen(p)+1;
        } else {
            if(nrp < 3) break;
            if(!_literal(rp[1], &n)) break;
            if((j = strindex(ops, rp[2])) == -1) break;
            delRpn(rp, &nrp, 2);
            rp[0] = p;
            sprintf(p, "%u", doOp(i, n, opOps[j], &nrp));
            p += strlen(p)+1;
        }
    }

    for(i = 0; i < nrp; i++) {
        j = strindex(ops, rp[i]);
        if(j == -1)
            push(rp[i]);
        else if(f = findFunction(rp[i]))
            push(rp[i]);
        else {
            dbprintf("%s\n", opsIns[j]);
            memory[nmemory++] = opOps[j];
            if(opOps[j]&0xff00)
                memory[nmemory++] = opOps[j]>>8;
        }
    }

    return r;
}

int value(const char *end, const char *end1, int *n) {
    char buf[EXBUFSZ];
    char *p;
    char *rp[100];
    int stack[20];
    int nrp, r, i, j, sp;
    nrp = 0;
    struct function *f;

    p = buf;
    r = _expression(end, end1, rp, &nrp, &p);

    sp = 0;
    for(i = 0; i < nrp; i++) {
        if((j = strindex(litOps, rp[i])) == -1)
            stack[sp++] = literal(rp[i]);
        else {
            stack[sp-2] = doOp(stack[sp-2], stack[sp-1], opOps[j], &sp);
            sp--;
        }
    }

    if(sp != 1) error("invalid immediate expression");
    *n = stack[0];

    return r;
}

char *addName(char *s) {
    char *old = namep;
    strcpy(namep, s);
    namep += strlen(s)+1;
    return old;
}

int compileNext(const char *until);

void compileUntil(const char *until) {
    while(!compileNext(until))
        if(feof(fp)) expect("<EOF>", until);
}

int asmVal(char *s) {
    int i;
    for(i = 0; i < 0x40; i++)
        if(insStrs[i] && !strcmp(insStrs[i], s)) return i;
    if(number(s, &i)) return i;
    printf("%s ?\n", s);
    error("invalid value in assembler");
}

void pcasm() {
    char buf[BUFSZ];
    for(;;) {
        getNext(buf);
        if(!strcmp(buf, "END")) return;
        if(feof(fp)) error("asm got eof");
        switch(*buf) {
        case '!':
            *(int16_t*)&memory[nmemory] = asmVal(buf+1);
            nmemory += 2;
            break;
        case '#':
            *(int32_t*)&memory[nmemory] = asmVal(buf+1);
            nmemory += 4;
            break;
        default:
            memory[nmemory++] = asmVal(buf);
            break;
        }
    }
}

void resolveBreaks() {
    csp--;
    while(cstack[--csp])
        *(int16_t*)&memory[cstack[csp]] = nmemory-cstack[csp]-2;
}

void pushLoop() {
    cstack[csp++] = 0; // breaks nullterm
    cstack[csp++] = nmemory; // continue addr
}

void addBreak() {
    memory[nmemory++] = INS_JMP;
    cstack[csp] = cstack[csp-1];
    cstack[csp-1] = nmemory;
    csp++;
    nmemory += 2;
}

void addContinue() {
    memory[nmemory++] = INS_JMP;
    *(int16_t*)&memory[nmemory] = cstack[csp-1]-nmemory-2;
    nmemory += 2;
}

int insSz(unsigned char ins);

void adjustContinues(int i1, int val) {
    int i;
    for(i = i1; i < nmemory; i += insSz(memory[i]))
        if(memory[i] == INS_JMP &&
          *(int16_t*)&memory[i+1]+i+3 == cstack[csp-1])
            *(int16_t*)&memory[i+1] = val-i-3;
}

void compileFile(const char *filename);

int compileNext(const char *until) {
    char buf[BUFSZ];
    char name[BUFSZ];
    int n, r, a1, a2, i;
    struct function *f;

    getNext(name);
    if(name[0] == 0) return 0;
    r = !strcmp(name, until);

    if(!strcmp(name, "FUNCTION")) {
        getNext(name);
        getNext(buf);
        n = expect1(buf, ";", "(");

        if(f = findFunction(name)) {
            lastFunction = f;
            f->addr = nmemory;
            if(n) {
                n = 0;
                do {
                    getNext(buf);
                    if(strcmp(f->args[n++], buf))
                        error("different args");
                    getNext(buf);
                } while(expect1(buf, ")", ","));
                next(";");
            }
            if(n != f->nargs) error("different no. of args");
        } else {
            f = &functions[nfunctions];
            if(n) {
                n = 0;
                do {
                    getNext(buf);
                    f->args[n++] = addName(buf);
                    getNext(buf);
                } while(expect1(buf, ")", ","));
                next(";");
            }
            f->s = addName(name);
            f->nargs = n;
            f->nvars = 0;
            f->addr = nmemory;
            lastFunction = &functions[nfunctions++];
            dbprintf(":f%s\n", name);
        }
    } else if(!strcmp(name, "VAR")) {
        f = lastFunction;
        f->nvars = 0;
        a1 = -4;
        for(;;) {
            getNext(buf);
            f->varp[f->nvars] = a1;
            f->varn[f->nvars] = 0;
            a1 -= 4;
            f->vars[f->nvars++] = addName(buf);

            getNext(buf);
            if(!strcmp(buf, ";")) break;
            if(!strcmp(buf, ".")) {
                n = value(",", ";", &f->varn[f->nvars-1]);
                a1 -= f->varn[f->nvars-1]-4;
                if(n) break;
            } else if(!strcmp(buf, ":")) {
                n = value(",", ";", &f->varn[f->nvars-1]);
                f->varn[f->nvars-1] * 4;
                a1 -= f->varn[f->nvars-1]-4;
                if(n) break;
            } else expect(buf, ",");
        }
    } else if(!strcmp(name, "BEGIN")) {
        if(scope++ == 0) {
            n = functionStack(lastFunction)*-1;
            dbprintf("pushbp\nmovbpsp\naddsp !%d\n", n);
            memory[nmemory++] = INS_PUSHBP;
            memory[nmemory++] = INS_MOVBPSP;
            memory[nmemory++] = INS_ADDSP;
            *(int16_t*)&memory[nmemory] = n;
            nmemory += 2;
        }
        compileUntil("END");
        if(--scope == 0) {
            for(i = 0; i < nexits; i++)
                *(int16_t*)&memory[exits[i]] = nmemory-exits[i]-2;
            nexits = 0;

            dbprintf("movspbp\npopbp\npopra\naddsp !%d\njra\n",
              lastFunction->nargs*4);
            memory[nmemory++] = INS_MOVSPBP;
            memory[nmemory++] = INS_POPBP;
            memory[nmemory++] = INS_POPRA;
            memory[nmemory++] = INS_ADDSP;
            *(int16_t*)&memory[nmemory] = lastFunction->nargs*4;
            nmemory += 2;
            memory[nmemory++] = INS_JRA;
        }
    } else if(!strcmp(name, "END")) {
        next(";");
    } else if(!strcmp(name, "IF")) {
        expression("THEN", 0);
        memory[nmemory++] = INS_JZ;
        a1 = nmemory;
        nmemory += 2;
        n = ++nlabels;
        dbprintf("jz @l%d\n", n);
        compileNext("");
        *(int16_t*)&memory[a1] = nmemory-a1-2;
        dbprintf(":l%d\n", n);
    } else if(!strcmp(name, "ELSE")) {
        if(a1 == -1) error("expected IF before ELSE");
        dbprintf(":l%d\n", n);
        n = ++nlabels;
        dbprintf("jmp @l%d\n", n);
        memory[nmemory++] = INS_JMP;
        a2 = nmemory;
        nmemory += 2;
        *(int16_t*)&memory[a1] = nmemory-a1-2;
        compileNext("");
        *(int16_t*)&memory[a2] = nmemory-a2-2;
        dbprintf(":l%d\n", n);
    } else if(!strcmp(name, "WHILE")) {
        n = nlabels;
        nlabels += 2;
        dbprintf(":l%d\n", n);
        pushLoop();
        a1 = nmemory;
        expression("DO", 0);
        dbprintf("jz @l%d\n", n+1);
        memory[nmemory++] = INS_JZ;
        a2 = nmemory;
        nmemory += 2;
        compileNext("");
        dbprintf("jmp @l%d\n", n);
        memory[nmemory++] = INS_JMP;
        *(int16_t*)&memory[nmemory] = a1-nmemory-2;
        nmemory += 2;
        *(int16_t*)&memory[a2] = nmemory-a2-2;
        dbprintf(":l%d\n", n+1);
        resolveBreaks();
    } else if(!strcmp(name, "FOR")) {
        getNext(name);
        next(":=");
        expression("TO", 0);
        pop(name);
        n = nlabels;
        nlabels += 2;
        dbprintf(":l%d\n", n);
        pushLoop();
        a1 = nmemory;
        push(name);
        expression("DO", 0);
        dbprintf("lte\njz @l%d\n", n+1);
        memory[nmemory++] = INS_LTE;
        memory[nmemory++] = INS_JZ;
        a2 = nmemory;
        nmemory += 2;
        nlabels++;
        compileNext("");
        adjustContinues(a2, nmemory);
        push(name);
        dbprintf("inc\n");
        memory[nmemory++] = INS_INC;
        pop(name);
        dbprintf("jmp @l%d\n", n);
        memory[nmemory++] = INS_JMP;
        *(int16_t*)&memory[nmemory] = a1-nmemory-2;
        nmemory += 2;
        *(int16_t*)&memory[a2] = nmemory-a2-2;
        dbprintf(":l%d\n", n+1);
        resolveBreaks();
    } else if(!strcmp(name, "FOREVER")) {
        n = nlabels++;
        a1 = nmemory;
        dbprintf(":l%d\n", n);
        pushLoop();
        compileNext("");
        dbprintf("jmp @l%d\n", n);
        memory[nmemory++] = INS_JMP;
        *(int16_t*)&memory[nmemory] = a1-nmemory-2;
        nmemory += 2;
        resolveBreaks();
    } else if(!strcmp(name, "DO")) {
        n = nlabels++;
        a1 = nmemory;
        dbprintf(":l%d\n", n);
        pushLoop();
        compileNext("");
        adjustContinues(a1, nmemory);
        next("WHILE");
        expression(";", 0);
        dbprintf("jz @l%d\n", n);
        memory[nmemory++] = INS_JNZ;
        *(int16_t*)&memory[nmemory] = a1-nmemory-2;
        nmemory += 2;
        resolveBreaks();
    } else if(!strcmp(name, "ASM")) {
        pcasm();
        next(";");
    } else if(!strcmp(name, "BREAK")) {
        addBreak();
        next(";");
    } else if(!strcmp(name, "CONTINUE")) {
        addContinue();
        next(";");
    } else if(!strcmp(name, "INCLUDE")) {
        getNext(buf);
        next(";");
        if(*buf != '"') error("expected string");
        compileFile(buf+1);
    } else if(!strcmp(name, "EXIT")) {
        memory[nmemory++] = INS_JMP;
        exits[nexits++] = nmemory;
        nmemory += 2;
        next(";");
    } else {
        getNext(buf);
        if(!strcmp(buf, ":=")) {
            expression(";", 0);
            pop(name);
        } else if(!strcmp(buf, ":")) {
            push(name);
            expression(":=", 0);
            dbprintf("add4\n");
            memory[nmemory++] = INS_ADD4;
            expression(";", 0);
            dbprintf("sw\n");
            memory[nmemory++] = INS_SW;
        } else if(!strcmp(buf, ".")) {
            push(name);
            expression(":=", 0);
            dbprintf("add\n");
            memory[nmemory++] = INS_ADD;
            expression(";", 0);
            dbprintf("sb\n");
            memory[nmemory++] = INS_SB;
        } else {
            n = expect1(buf, ";", "(");
            if(n) {
                while(expression(")", ",")) n++;
                next(";");
            }
            f = findFunction(name);
            if(!f) error("unknown function");
            if(f->nargs != n) error("wrong no. of args");
            dbprintf("jal #f%s\n", name);
            memory[nmemory++] = INS_JAL;
            *(uint32_t*)&memory[nmemory] = f-functions;
            nmemory += 4;
        }
    }

    if(strcmp(name, "IF")) a1 = -1;

    return r;
}

void ext(char *buf, const char *s1, const char *ext, char ass) {
    int i;
    strcpy(buf, s1);
    for(i = strlen(buf)-1; i > 0 && buf[i] != '.'; i--);
    if(i) {
        if(!ass) return;
        buf[i] = 0;
    }
    strcpy(buf+strlen(buf), ext);
}

void compileFile(const char *fn) {
    const char *pfn;
    FILE *pfp;
    int pln;
    pfp = fp;
    pfn = filename;
    pln = lineNo;
    lineNo = 1;
    filename = fn;
    fp = fopen(filename, "r");
    if(!fp) {
        printf("failed to open %s\n", filename);
        return;
    }
    while(!feof(fp))
        compileNext("");
    fclose(fp);
    fp = pfp;
    filename = pfn;
    lineNo = pln;
}

int insSz(unsigned char ins) {
    if(ins&0xf0) return 1;
    return 1+(2<<(ins>>3));
}

void rectifyCalls() {
    int i;
    for(i = 0; i < nmemory; i += insSz(memory[i]))
        if(memory[i] == INS_JAL)
	    *(uint32_t*)&memory[i+1] =
              functions[*(uint32_t*)&memory[i+1]].addr+ORG;
}

void saveFile(const char *filename) {
    uint32_t n;
    fp = fopen(filename, "wb");
    if(!fp) {
        printf("failed to open %s\n", filename);
        return;
    }
    n = ORG;
    fwrite(&n, 4, 1, fp);
    fwrite(&nmemory, 4, 1, fp);
    fwrite(memory, 1, nmemory, fp);
    fwrite(data, 1, ndata, fp);
    fclose(fp);
}

void initMain() {
    functions[nfunctions].s = "MAIN";
    functions[nfunctions].nargs = 0;
    functions[nfunctions].nvars = 0;
    functions[nfunctions].addr = 0;
    nfunctions++;
    memory[nmemory++] = INS_JAL;
    *(uint32_t*)&memory[nmemory] = 0;
    nmemory += 4;
    memory[nmemory++] = INS_PUSHRV;
    memory[nmemory++] = INS_SYS;
    *(int16_t*)&memory[nmemory] = 0;
    nmemory += 2;
}

int main(int argc, char **args) {
    char buf[BUFSZ];
    char *s;

    if(argc > 2 && !strcmp(args[argc-1], "-d")) {
        argc--;
        debug = 1;
    }

    if(argc != 2) {
        printf("usage: %s <file>\n", args[0]);
        return 0;
    }

    initMain();
    ext(buf, args[1], ".pk", 0);
    compileFile(buf);
    rectifyCalls();
    ext(buf, args[1], ".pc", 1);
    saveFile(buf);
    return 0;
}
