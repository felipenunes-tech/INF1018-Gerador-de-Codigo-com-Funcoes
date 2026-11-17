/* Felipe Nunes 2320615 3WA */
/* Manuela Borba 2510272 3WA */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gera_codigo.h"

#define MAX_LINHA 256
#define MAX_FUNCS 50

/*
 * Layout da pilha no registro de ativacao (em relacao a %rbp):
 *   v0: -4(%rbp)
 *   v1: -8(%rbp)
 *   v2: -12(%rbp)
 *   v3: -16(%rbp)
 *   v4: -20(%rbp)
 *   p0: -24(%rbp) (copia de %edi salva no prologo)
 *
 * Alocamos 32 bytes de frame (subq $32, %rsp) para acomodar
 * todas as variaveis e manter %rsp alinhado em 16 bytes antes
 * de eventuais chamadas (call), atendendo a ABI x86-64 do Linux.
 */

/* Emite um inteiro de 32 bits em formato little-endian */
static void emite_int32(unsigned char code[], int *pos, int val)
{
    unsigned int u = (unsigned int)val;
    code[(*pos)++] = (unsigned char)(u & 0xFF);
    code[(*pos)++] = (unsigned char)((u >> 8) & 0xFF);
    code[(*pos)++] = (unsigned char)((u >> 16) & 0xFF);
    code[(*pos)++] = (unsigned char)((u >> 24) & 0xFF);
}

/* Retorna o deslocamento relativo a %rbp de uma variavel ou parametro */
static int calc_offset(char tipo, int idx)
{
    if (tipo == 'p')
        return -24;             /* p0 salvo em -24(%rbp) */
    return -4 * (idx + 1);      /* v0 em -4, v1 em -8, ..., v4 em -20 */
}

/* Carrega operando (constante $, parametro p ou variavel v) no registrador %eax */
static void carrega_eax(unsigned char code[], int *pos, char tipo, int val)
{
    if (tipo == '$') {
        /* movl $val, %eax (b8 <imm32>) */
        code[(*pos)++] = 0xb8;
        emite_int32(code, pos, val);
    } else {
        /* movl disp8(%rbp), %eax (8b 45 <disp8>) */
        code[(*pos)++] = 0x8b;
        code[(*pos)++] = 0x45;
        code[(*pos)++] = (unsigned char)calc_offset(tipo, val);
    }
}

/* Carrega operando no registrador %ecx */
static void carrega_ecx(unsigned char code[], int *pos, char tipo, int val)
{
    if (tipo == '$') {
        /* movl $val, %ecx (b9 <imm32>) */
        code[(*pos)++] = 0xb9;
        emite_int32(code, pos, val);
    } else {
        /* movl disp8(%rbp), %ecx (8b 4d <disp8>) */
        code[(*pos)++] = 0x8b;
        code[(*pos)++] = 0x4d;
        code[(*pos)++] = (unsigned char)calc_offset(tipo, val);
    }
}

/* Carrega operando no registrador %edi (primeiro argumento para chamada de funcao) */
static void carrega_edi(unsigned char code[], int *pos, char tipo, int val)
{
    if (tipo == '$') {
        /* movl $val, %edi (bf <imm32>) */
        code[(*pos)++] = 0xbf;
        emite_int32(code, pos, val);
    } else {
        /* movl disp8(%rbp), %edi (8b 7d <disp8>) */
        code[(*pos)++] = 0x8b;
        code[(*pos)++] = 0x7d;
        code[(*pos)++] = (unsigned char)calc_offset(tipo, val);
    }
}

/* Salva o valor de %eax na variavel local v<idx> (movl %eax, disp8(%rbp)) */
static void salva_var(unsigned char code[], int *pos, int idx)
{
    code[(*pos)++] = 0x89;
    code[(*pos)++] = 0x45;
    code[(*pos)++] = (unsigned char)calc_offset('v', idx);
}

/*
 * Prologo padrao da funcao:
 *   pushq %rbp              (55)
 *   movq  %rsp, %rbp        (48 89 e5)
 *   subq  $32, %rsp         (48 83 ec 20)
 *   movl  %edi, -24(%rbp)   (89 7d e8)
 */
static void gera_prologo(unsigned char code[], int *pos)
{
    code[(*pos)++] = 0x55;
    code[(*pos)++] = 0x48; code[(*pos)++] = 0x89; code[(*pos)++] = 0xe5;
    code[(*pos)++] = 0x48; code[(*pos)++] = 0x83; code[(*pos)++] = 0xec; code[(*pos)++] = 0x20;
    code[(*pos)++] = 0x89; code[(*pos)++] = 0x7d; code[(*pos)++] = 0xe8;
}

/*
 * Epilogo padrao da funcao:
 *   leave                   (c9)
 *   ret                     (c3)
 */
static void gera_epilogo(unsigned char code[], int *pos)
{
    code[(*pos)++] = 0xc9;
    code[(*pos)++] = 0xc3;
}

/* Extrai o tipo ('$', 'v' ou 'p') e o valor numerico de uma string de operando */
static void interpreta_operando(const char *str, char *tipo, int *val)
{
    *tipo = str[0];
    if (*tipo == '$' || *tipo == 'v')
        *val = atoi(str + 1);
    else
        *val = 0; /* 'p' -> p0 */
}

/* Traduz atribuicao com operacao aritmetica: v<dst> = op1 <operador> op2 */
static void gera_operacao(unsigned char code[], int *pos, int dst,
                          char tipo1, int val1, char op, char tipo2, int val2)
{
    carrega_eax(code, pos, tipo1, val1);
    carrega_ecx(code, pos, tipo2, val2);

    switch (op) {
    case '+':
        /* addl %ecx, %eax (01 c8) */
        code[(*pos)++] = 0x01;
        code[(*pos)++] = 0xc8;
        break;
    case '-':
        /* subl %ecx, %eax (29 c8) */
        code[(*pos)++] = 0x29;
        code[(*pos)++] = 0xc8;
        break;
    case '*':
        /* imull %ecx, %eax (0f af c1) */
        code[(*pos)++] = 0x0f;
        code[(*pos)++] = 0xaf;
        code[(*pos)++] = 0xc1;
        break;
    }

    salva_var(code, pos, dst);
}

/* Traduz chamada de funcao: v<dst> = call <num_func> <arg> */
static void gera_call(unsigned char code[], int *pos, int dst,
                      int num_func, char tipo_arg, int val_arg, const int offsets[])
{
    int rel;

    /* Passa o argumento varpc em %edi */
    carrega_edi(code, pos, tipo_arg, val_arg);

    /* call rel32 (e8 <disp32>) */
    code[(*pos)++] = 0xe8;
    rel = offsets[num_func] - (*pos + 4);
    emite_int32(code, pos, rel);

    /* Guarda o valor de retorno (%eax) na variavel de destino */
    salva_var(code, pos, dst);
}

/* Traduz retorno incondicional: ret <arg> */
static void gera_ret(unsigned char code[], int *pos, char tipo, int val)
{
    carrega_eax(code, pos, tipo, val);
    gera_epilogo(code, pos);
}

/* Traduz retorno condicional: zret <cond> <val> (retorna val se cond == 0) */
static void gera_zret(unsigned char code[], int *pos,
                      char tipo_cond, int val_cond, char tipo_val, int val_val)
{
    int pos_salto;

    /* Carrega o primeiro operando e testa se e zero */
    carrega_eax(code, pos, tipo_cond, val_cond);
    code[(*pos)++] = 0x83; code[(*pos)++] = 0xf8; code[(*pos)++] = 0x00; /* cmpl $0, %eax */

    /* Salta o retorno caso nao seja zero (jne rel8) */
    code[(*pos)++] = 0x75;
    pos_salto = (*pos)++;

    /* Se for zero, executa o retorno */
    gera_ret(code, pos, tipo_val, val_val);

    /* Preenche o deslocamento do salto para continuar o fluxo */
    code[pos_salto] = (unsigned char)(*pos - (pos_salto + 1));
}

/* Funcao principal geradora de codigo */
void gera_codigo(FILE *f, unsigned char code[], funcp *entry)
{
    char linha[MAX_LINHA];
    int offsets[MAX_FUNCS];
    int num_funcs = 0;
    int pos = 0;

    *entry = NULL;

    while (fgets(linha, sizeof(linha), f) != NULL) {
        char *p = linha;
        char token1[32], token2[32], token3[32];
        int dst;

        /* Pula espacos ou tabulacoes no inicio da linha */
        while (*p == ' ' || *p == '\t')
            p++;

        /* Ignora linhas em branco */
        if (*p == '\n' || *p == '\r' || *p == '\0')
            continue;

        if (strncmp(p, "function", 8) == 0) {
            offsets[num_funcs++] = pos;
            gera_prologo(code, &pos);
        }
        else if (strncmp(p, "end", 3) == 0) {
            /* Fim da funcao; o retorno com epilogo ja foi emitido pelo ret */
        }
        else if (strncmp(p, "ret", 3) == 0) {
            char arg[32];
            char tipo;
            int val;
            if (sscanf(p, "ret %31s", arg) == 1) {
                interpreta_operando(arg, &tipo, &val);
                gera_ret(code, &pos, tipo, val);
            }
        }
        else if (strncmp(p, "zret", 4) == 0) {
            char cond[32], retv[32];
            char tipo_cond, tipo_retv;
            int val_cond, val_retv;
            if (sscanf(p, "zret %31s %31s", cond, retv) == 2) {
                interpreta_operando(cond, &tipo_cond, &val_cond);
                interpreta_operando(retv, &tipo_retv, &val_retv);
                gera_zret(code, &pos, tipo_cond, val_cond, tipo_retv, val_retv);
            }
        }
        else if (sscanf(p, "v%d = %31s %31s %31s", &dst, token1, token2, token3) == 4) {
            if (strcmp(token1, "call") == 0) {
                int num_chamada = atoi(token2);
                char tipo_arg;
                int val_arg;
                interpreta_operando(token3, &tipo_arg, &val_arg);
                gera_call(code, &pos, dst, num_chamada, tipo_arg, val_arg, offsets);
            } else {
                char tipo1, tipo2;
                int val1, val2;
                char op = token2[0];
                interpreta_operando(token1, &tipo1, &val1);
                interpreta_operando(token3, &tipo2, &val2);
                gera_operacao(code, &pos, dst, tipo1, val1, op, tipo2, val2);
            }
        }
    }

    if (num_funcs > 0)
        *entry = (funcp)(code + offsets[num_funcs - 1]);
}
