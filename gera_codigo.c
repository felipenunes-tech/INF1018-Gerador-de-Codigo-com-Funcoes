/* Felipe Nunes 2320615 3WA */
/* Nome_do_Aluno2 Matricula 3WA */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gera_codigo.h"

#define MAX_LINHA   256
#define MAX_FUNCS   50
#define NUM_VARS    5

/* Layout do registro de ativacao (relativo a %rbp):
 *   v0 = -4(%rbp), v1 = -8(%rbp), ..., v4 = -20(%rbp)
 *   p0 (copia do %edi) = -24(%rbp)
 * Frame de 32 bytes: mantem %rsp alinhado em 16 bytes nas chamadas. */
#define OFFSET_P0   (-4 * (NUM_VARS + 1))
#define TAM_FRAME   32

/* Numero do registrador (campo reg do ModRM / soma no opcode mov imm) */
typedef enum { REG_EAX = 0, REG_ECX = 1, REG_EDI = 7 } Reg;

typedef enum { OPD_VAR, OPD_PARAM, OPD_CONST } TipoOperando;

typedef struct {
    TipoOperando tipo;
    int valor; /* indice da variavel ou valor da constante */
} Operando;

typedef struct {
    unsigned char *codigo;
    int tamanho;
} Gerador;

static void emite_byte(Gerador *gerador, unsigned char byte)
{
    gerador->codigo[gerador->tamanho++] = byte;
}

static void emite_int32(Gerador *gerador, int valor)
{
    unsigned int u = (unsigned int)valor;
    int parte;
    for (parte = 0; parte < 4; parte++)
        emite_byte(gerador, (unsigned char)((u >> (8 * parte)) & 0xFF));
}

static int deslocamento_operando(Operando op)
{
    if (op.tipo == OPD_PARAM)
        return OFFSET_P0;
    return -4 * (op.valor + 1);
}

/* mov <op>, %reg   (op pode ser constante, variavel ou parametro) */
static void emite_carrega(Gerador *gerador, Reg reg, Operando op)
{
    if (op.tipo == OPD_CONST) {
        emite_byte(gerador, 0xb8 + reg); /* mov $imm32, %reg */
        emite_int32(gerador, op.valor);
    } else {
        emite_byte(gerador, 0x8b); /* mov disp8(%rbp), %reg */
        emite_byte(gerador, 0x45 | (reg << 3));
        emite_byte(gerador, (unsigned char)deslocamento_operando(op));
    }
}

/* mov %reg, v<idx> */
static void grava_variavel(Gerador *gerador, Reg reg, int indice)
{
    Operando variavel = { OPD_VAR, indice };
    emite_byte(gerador, 0x89);    /* mov %reg, disp8(%rbp) */
    emite_byte(gerador, 0x45 | (reg << 3));
    emite_byte(gerador, (unsigned char)deslocamento_operando(variavel));
}

/* push %rbp; mov %rsp,%rbp; sub $32,%rsp; mov %edi,-24(%rbp) */
static void abre_funcao(Gerador *gerador)
{
    Operando p0 = { OPD_PARAM, 0 };
    emite_byte(gerador, 0x55);
    emite_byte(gerador, 0x48); emite_byte(gerador, 0x89); emite_byte(gerador, 0xe5);
    emite_byte(gerador, 0x48); emite_byte(gerador, 0x83); emite_byte(gerador, 0xec);
    emite_byte(gerador, TAM_FRAME);
    emite_byte(gerador, 0x89); emite_byte(gerador, 0x7d);
    emite_byte(gerador, (unsigned char)deslocamento_operando(p0));
}

/* leave; ret */
static void fecha_funcao(Gerador *gerador)
{
    emite_byte(gerador, 0xc9);
    emite_byte(gerador, 0xc3);
}

/* v<destino> = primeiro operador segundo */
static void gera_operacao(Gerador *gerador, int destino, Operando primeiro, char operador, Operando segundo)
{
    emite_carrega(gerador, REG_EAX, primeiro);
    emite_carrega(gerador, REG_ECX, segundo);
    switch (operador) {
    case '+': emite_byte(gerador, 0x01); emite_byte(gerador, 0xc8); break; /* add %ecx,%eax  */
    case '-': emite_byte(gerador, 0x29); emite_byte(gerador, 0xc8); break; /* sub %ecx,%eax  */
    case '*': emite_byte(gerador, 0x0f); emite_byte(gerador, 0xaf);
 emite_byte(gerador, 0xc1); break; /* imul %ecx,%eax */
    }
    grava_variavel(gerador, REG_EAX, destino);
}

/* v<destino> = call funcao argumento */
static void gera_chamada(Gerador *gerador, int destino, int funcao, Operando argumento, const int inicio_funcoes[])
{
    int distancia;
    emite_carrega(gerador, REG_EDI, argumento);
    emite_byte(gerador, 0xe8); /* call rel32 */
    distancia = inicio_funcoes[funcao] - (gerador->tamanho + 4);
    emite_int32(gerador, distancia);
    grava_variavel(gerador, REG_EAX, destino);
}

/* ret a */
static void gera_retorno(Gerador *gerador, Operando valor)
{
    emite_carrega(gerador, REG_EAX, valor);
    fecha_funcao(gerador);
}

/* zret a b: se a == 0 retorna b */
static void gera_retorno_condicional(Gerador *gerador, Operando condicao, Operando valor)
{
    int desvio;
    emite_carrega(gerador, REG_EAX, condicao);
    emite_byte(gerador, 0x83); emite_byte(gerador, 0xf8); emite_byte(gerador, 0x00); /* cmp $0,%eax */
    emite_byte(gerador, 0x75);        /* jne rel8 */
    desvio = gerador->tamanho;
    emite_byte(gerador, 0x00);        /* placeholder */
    gera_retorno(gerador, valor);
    gerador->codigo[desvio] = (unsigned char)(gerador->tamanho - (desvio + 1));
}

static Operando le_operando(const char *texto)
{
    Operando op;
    op.valor = 0;
    switch (texto[0]) {
    case 'v': op.tipo = OPD_VAR;   op.valor = atoi(texto + 1); break;
    case 'p': op.tipo = OPD_PARAM; break;
    default:  op.tipo = OPD_CONST; op.valor = atoi(texto + 1); break; /* '$' */
    }
    return op;
}

static void le_retorno(Gerador *gerador, const char *linha)
{
    char retorno[32];
    sscanf(linha, "ret %31s", retorno);
    gera_retorno(gerador, le_operando(retorno));
}

static void le_retorno_condicional(Gerador *gerador, const char *linha)
{
    char condicao[32], retorno[32];
    sscanf(linha, "zret %31s %31s", condicao, retorno);
    gera_retorno_condicional(gerador, le_operando(condicao), le_operando(retorno));
}

static void le_atribuicao(Gerador *gerador, const char *linha, const int inicio_funcoes[])
{
    int destino;
    char primeiro[32], operador[32], segundo[32];

    sscanf(linha, "v%d = %31s %31s %31s", &destino, primeiro, operador, segundo);
    if (strcmp(primeiro, "call") == 0)
        gera_chamada(gerador, destino, atoi(operador), le_operando(segundo), inicio_funcoes);
    else
        gera_operacao(gerador, destino, le_operando(primeiro), operador[0], le_operando(segundo));
}

void gera_codigo(FILE *f, unsigned char code[], funcp *entry)
{
    Gerador gerador;
    char linha[MAX_LINHA];
    int inicio_funcoes[MAX_FUNCS];
    int qtd_funcoes = 0;

    gerador.codigo = code;
    gerador.tamanho = 0;
    *entry = NULL;

    while (fgets(linha, sizeof linha, f) != NULL) {
        if (strncmp(linha, "function", 8) == 0) {
            inicio_funcoes[qtd_funcoes++] = gerador.tamanho;
            abre_funcao(&gerador);
        } else if (strncmp(linha, "end", 3) == 0) {
            /* nada a emitir: o ret da funcao ja gera o epilogo */
        } else if (strncmp(linha, "ret", 3) == 0) {
            le_retorno(&gerador, linha);
        } else if (strncmp(linha, "zret", 4) == 0) {
            le_retorno_condicional(&gerador, linha);
        } else if (linha[0] == 'v') {
            le_atribuicao(&gerador, linha, inicio_funcoes);
        }
    }

    if (qtd_funcoes > 0)
        *entry = (funcp)(code + inicio_funcoes[qtd_funcoes - 1]);
}