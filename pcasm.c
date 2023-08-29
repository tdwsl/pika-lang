#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct symbol {
    char *s;
    int a;
};

const char *insStrs[] = {
    "sys", "pushbpp", "popbpv", "pushbpv",
    "jz", "jnz", "jmp", "addsp",
    "push", "global", "jal", 0,
    0,0,0,0,
    "popra", "pushra", "poprv", "pushrv",
    "popbp", "pushbp", "movbpsp", "movspbp",
    "jra", "popgp", 0, 0,
    0,0,0,0,
    "inc", "dec", "inv", "neg",
    "lb", "lh", "lw", 0,
    "sb", "sh", "sw", 0,
    0,0,0,0,
    "add", "sub", "add4", "and",
    "or", "xor", "shl", "shr",
    "eq", "ne", "lt", "lte",
    "div", "mul", "mod", 0,
};

char symbolBuf[1024*128];
char *nextSymbol = symbolBuf;
struct symbol symbols[8096];
int nsymbols = 0;

unsigned char memory[65536];
int size = 0, org = 0;

int getNext(FILE *fp, char *buf) {
    char t;
    char *p = buf;
    for(;;) {
        *p = fgetc(fp);
        if(*p <= ' ' || feof(fp)) {
            t = *p;
            *p = 0;
            if(p != buf || feof(fp) || t == '\n')
                return (t=='\n');
        } else p++;
    }
}

void addSymbol(char *s) {
    if(*s == 0) return;
    symbols[nsymbols++] = (struct symbol) { nextSymbol, org, };
    strcpy(nextSymbol, s);
    nextSymbol += strlen(s)+1;
}

int hex(char *s, int *n) {
    do {
        *n *= 16;
        if(*s >= '0' && *s <= '9') *n += *s-'0';
        else if(*s >= 'A' && *s <= 'Z') *n += *s-'A'+10;
        else if(*s >= 'a' && *s <= 'z') *n += *s-'a'+10;
        else return 1;
    } while(*(++s));
    return 0;
}

int bin(char *s, int *n) {
    do {
        *n *= 2;
        if(*s == '1') (*n)++;
        else if(*s != '0') return 1;
    } while(*(++s));
    return 0;
}

int number(char *s, int *n) {
    int m = 0;

    if(*s == '-') { m = 1; s++; }
    if(*s == 0) return 0;

    *n = 0;

    switch(s[1]) {
    case 'x': if(hex(s+2, n)) return 0; break;
    case 'b': if(bin(s+2, n)) return 0; break;
    default:
        do {
            *n *= 10;
            if(*s >= '0' && *s <= '9') *n += *s-'0';
            else return 0;
        } while(*(++s));
        break;
    }

    if(m) *n *= -1;
    return 1;
}

int val(char *s, int ln) {
    int i;

    for(i = 0; i < 0x40; i++)
        if(insStrs[i])
            if(!strcmp(insStrs[i], s)) return i;

    for(i = 0; i < nsymbols; i++)
        if(!strcmp(symbols[i].s, s)) return symbols[i].a;

    if(number(s, &i)) return i;

    printf("value error on line %d: %s\n", ln, s);
    exit(1);
}

void pass1(const char *filename) {
    FILE *fp;
    char buf[200];
    int ln = 1;
    int nl;

    fp = fopen(filename, "r");
    if(!fp) { printf("failed to open %s\n", filename); exit(1); }

    while(!feof(fp)) {
        nl = getNext(fp, buf);

        switch(buf[0]) {
        case 0:
            break;
        case '/':
            pass1(buf+1);
            break;
        case '*':
            org = val(buf+1, ln);
            break;
        case ':':
            addSymbol(buf+1);
            break;
        case '=':
            symbols[nsymbols-1].a = val(buf+1, ln);
            break;
        case '+':
            symbols[nsymbols-1].a += val(buf+1, ln);
            break;
        case ';':
            while(!nl) nl = getNext(fp, buf);
            break;
        case '#':
            org += 4;
            break;
        case '"':
            org += strlen(buf)-1;
            break;
        case '@':
        case '!':
            org += 2;
            break;
        default:
            org++;
            break;
        }

        if(nl) ln++;
    }

    fclose(fp);
}

void pass2(const char *filename) {
    FILE *fp;
    char buf[200];
    int ln = 1;
    int nl, n;

    fp = fopen(filename, "r");
    if(!fp) { printf("failed to open %s\n", filename); exit(1); }

    printf("\n%.8X ", org);
    while(!feof(fp)) {
        nl = getNext(fp, buf);

        if(buf[0] != ';')
            printf("%s ", buf);

        switch(buf[0]) {
        case '/':
            pass2(buf+1);
            break;
        case '*':
            org = val(buf+1, ln);
            break;
        case 0:
        case ':':
        case '=':
        case '+':
            break;
        case ';':
            while(!nl) nl = getNext(fp, buf);
            break;
        case '#':
            *(uint32_t*)&memory[size] = val(buf+1, ln);
            org += 4;
            size += 4;
            break;
        case '"':
            strcpy(memory+size, buf+1);
            org += strlen(buf)-1;
            size += strlen(buf)-1;
            break;
        case '!':
            *(int16_t*)&memory[size] = val(buf+1, ln);
            org += 2;
            size += 2;
            break;
        case '@':
            *(int16_t*)&memory[size] = val(buf+1, ln)-2-org;
            org += 2;
            size += 2;
            break;
        default:
            memory[size] = val(buf, ln);
            org++;
            size++;
            break;
        }

        if(nl) {
            ln++;
            printf("\n%.8X ", org);
        }
    }

    if(!nl) printf("\n");

    fclose(fp);
}

void saveFile(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if(!fp) { printf("failed to open %s\n", filename); exit(1); }
    fwrite(memory, 1, size, fp);
    fclose(fp);
}

int main(int argc, char **args) {
    if(argc != 3) {
        printf("usage: %s <file.asm> <out>\n", args[0]);
        return 0;
    }

    org = 0;
    pass1(args[1]);
    org = 0;
    size = 0;
    pass2(args[1]);

    saveFile(args[2]);
    return 0;
}
