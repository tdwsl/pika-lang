#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MEMORY_SIZE 1024*1024*2

uint32_t pc, ra, rv, sp, bp, gp;

unsigned char memory[MEMORY_SIZE];

unsigned char debug = 0;

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

void printIns() {
    printf("%.8X %s", pc, insStrs[memory[pc]]);
    if(!(memory[pc]&0xf0)) {
        if(memory[pc]>>3)
            printf(" %.8X", *(uint32_t*)&memory[pc+1]);
        else
            printf(" %.4X", *(uint16_t*)&memory[pc+1]);
    }
    printf("\n");
}

void printStack() {
    uint32_t i;
    for(i = sp-2*4; i <= sp+5*4; i += 4) {
        if(i == sp && i == bp) printf("SP/BP    ");
        else if(i == sp) printf("SP       ");
        else if(i == bp) printf("BP       ");
        else printf("         ");
    }
    printf("\n");
    for(i = sp-2*4; i <= sp+5*4; i += 4) {
        printf("%.8X ", *(uint32_t*)&memory[i]);
    }
    printf("\n");
}

void run() {
    unsigned char ins;
    int32_t h, a;
    uint32_t w;

    for(;;) {
        if(debug) { printStack(); printIns(); }

        ins = memory[pc++];
        if(!(ins&0xf0)) {
            if(ins>>3) w = *(uint32_t*)&memory[pc];
            else h = *(int16_t*)&memory[pc];
            pc += 2<<(ins>>3);
        }

        switch(ins) {
        case INS_SYS:
            switch(h) {
            case 0: exit(0);
            case 1:
                printf("%c", *(int32_t*)&memory[sp]);
                sp += 4;
                break;
            case 2:
                printf("%d", *(int32_t*)&memory[sp]);
                sp += 4;
                break;
            }
            break;
        case INS_PUSHBPP:
            sp -= 4;
            *(int32_t*)&memory[sp] = bp+h;
            break;
        case INS_POPBPV:
            *(int32_t*)&memory[bp+h] = *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_PUSHBPV:
            sp -= 4;
            *(int32_t*)&memory[sp] = *(int32_t*)&memory[bp+h];
            break;
        case INS_JZ:
            if(!(*(int32_t*)&memory[sp])) pc += h;
            sp += 4;
            break;
        case INS_JNZ:
            if(*(int32_t*)&memory[sp]) pc += h;
            sp += 4;
            break;
        case INS_JMP: 
            pc += h;
            break;
        case INS_ADDSP:
            sp += h;
            break;
        case INS_PUSH:
            sp -= 4;
            *(int32_t*)&memory[sp] = w;
            break;
        case INS_GLOBAL:
            sp -= 4;
            *(int32_t*)&memory[sp] = gp+w;
            break;
        case INS_JAL:
            sp -= 4;
            *(int32_t*)&memory[sp] = pc;
            pc = w;
            break;
        case INS_POPRA:
            ra = *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_PUSHRA:
            sp -= 4;
            *(int32_t*)&memory[sp] = ra;
            break;
        case INS_POPRV:
            rv = *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_PUSHRV:
            sp -= 4;
            *(int32_t*)&memory[sp] = rv;
            break;

        case INS_POPBP:
            bp = *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_PUSHBP:
            sp -= 4;
            *(int32_t*)&memory[sp] = bp;
            break;
        case INS_MOVBPSP:
            bp = sp;
            break;
        case INS_MOVSPBP:
            sp = bp;
            break;
        case INS_JRA:
            pc = ra;
            break;
        case INS_POPGP:
            gp = *(int32_t*)&memory[sp];
            sp += 4;
            break;

        case INS_INC:
            (*(int32_t*)&memory[sp])++;
            break;
        case INS_DEC:
            (*(int32_t*)&memory[sp])--;
            break;
        case INS_INV:
            *(int32_t*)&memory[sp] = ~*(int32_t*)&memory[sp];
            break;
        case INS_NEG:
            *(int32_t*)&memory[sp] = (~*(int32_t*)&memory[sp])+1;
            break;
        case INS_LB:
            *(int32_t*)&memory[sp] = (char)memory[*(int32_t*)&memory[sp]];
            break;
        case INS_LH:
            *(int32_t*)&memory[sp] =
              *(int16_t*)&memory[*(int32_t*)&memory[sp]];
            break;
        case INS_LW:
            *(int32_t*)&memory[sp] =
              *(int32_t*)&memory[*(int32_t*)&memory[sp]];
            break;
        case INS_SB:
            memory[*(int32_t*)&memory[sp+4]] = *(int32_t*)&memory[sp];
            sp += 8;
            break;
        case INS_SH:
            *(int16_t*)&memory[*(int32_t*)&memory[sp+4]] =
              *(int32_t*)&memory[sp];
            sp += 8;
            break;
        case INS_SW:
            *(int32_t*)&memory[*(int32_t*)&memory[sp+4]] =
              *(int32_t*)&memory[sp];
            sp += 8;
            break;

        case INS_ADD:
            *(int32_t*)&memory[sp+4] += *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_SUB:
            *(int32_t*)&memory[sp+4] -= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_ADD4:
            *(int32_t*)&memory[sp+4] += (*(int32_t*)&memory[sp])*4;
            sp += 4;
            break;
        case INS_AND:
            *(int32_t*)&memory[sp+4] &= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_OR:
            *(int32_t*)&memory[sp+4] |= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_XOR:
            *(int32_t*)&memory[sp+4] ^= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_SHL:
            *(int32_t*)&memory[sp+4] <<= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_SHR:
            *(int32_t*)&memory[sp+4] >>= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_EQ:
            *(int32_t*)&memory[sp+4] =
              *(int32_t*)&memory[sp+4] == *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_NE:
            *(int32_t*)&memory[sp+4] =
              (*(int32_t*)&memory[sp+4] != *(int32_t*)&memory[sp])*-1;
            sp += 4;
            break;
        case INS_LT:
            *(int32_t*)&memory[sp+4] =
              (*(int32_t*)&memory[sp+4] <  *(int32_t*)&memory[sp])*-1;
            sp += 4;
            break;
        case INS_LTE:
            *(int32_t*)&memory[sp+4] =
              (*(int32_t*)&memory[sp+4] <= *(int32_t*)&memory[sp])*-1;
            sp += 4;
            break;
        case INS_DIV:
            *(int32_t*)&memory[sp+4] /= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_MUL:
            *(int32_t*)&memory[sp+4] *= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        case INS_MOD:
            *(int32_t*)&memory[sp+4] %= *(int32_t*)&memory[sp];
            sp += 4;
            break;
        }
    }
}

void loadFile(const char *filename) {
    FILE *fp;
    fp = fopen(filename, "rb");
    if(!fp) {
        printf("failed to open %s\n", filename);
        exit(1);
    }
    fread(&pc, 4, 1, fp);
    sp = pc;
    bp = pc;
    fread(&gp, 4, 1, fp);
    fread(&memory[pc], 1, gp, fp);
    gp += pc;
    fread(&memory[gp], 1, MEMORY_SIZE-gp, fp);
}

int main(int argc, char **args) {
    if(!strcmp(args[argc-1], "-d")) {
        debug = 1;
        argc--;
    }
    if(argc != 2) {
        printf("usage: %s <file>\n", args[0]);
        return 0;
    }
    loadFile(args[1]);
    run();
    return 0;
}
