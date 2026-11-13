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
    int valor;              /* indice da variavel ou valor da constante */
} Operando;

typedef struct {
    unsigned char *buf;     /* vetor code passado pelo usuario */
    int pos;                /* proxima posicao livre */
} Codigo;

/* ------------------------------------------------------------------ */
/* Modulo 1: emissao de bytes                                          */
/* ------------------------------------------------------------------ */

static void emite_byte(Codigo *c, unsigned char b)
{
    c->buf[c->pos++] = b;
}

static void emite_int32(Codigo *c, int v)
{
    unsigned int u = (unsigned int)v;
    int i;
    for (i = 0; i < 4; i++)
        emite_byte(c, (unsigned char)((u >> (8 * i)) & 0xFF));
}

/* ------------------------------------------------------------------ */
/* Modulo 2: instrucoes x86-64 basicas                                 */
/* ------------------------------------------------------------------ */

static int offset_operando(Operando op)
{
    if (op.tipo == OPD_PARAM)
        return OFFSET_P0;
    return -4 * (op.valor + 1);
}

/* mov <op>, %reg   (op pode ser constante, variavel ou parametro) */
static void emite_carrega(Codigo *c, Reg r, Operando op)
{
    if (op.tipo == OPD_CONST) {
        emite_byte(c, 0xb8 + r);                 /* mov $imm32, %reg */
        emite_int32(c, op.valor);
    } else {
        emite_byte(c, 0x8b);                     /* mov disp8(%rbp), %reg */
        emite_byte(c, 0x45 | (r << 3));
        emite_byte(c, (unsigned char)offset_operando(op));
    }
}

/* mov %reg, v<idx> */
static void emite_guarda_var(Codigo *c, Reg r, int idx)
{
    Operando v = { OPD_VAR, idx };
    emite_byte(c, 0x89);                         /* mov %reg, disp8(%rbp) */
    emite_byte(c, 0x45 | (r << 3));
    emite_byte(c, (unsigned char)offset_operando(v));
}

/* push %rbp; mov %rsp,%rbp; sub $32,%rsp; mov %edi,-24(%rbp) */
static void emite_prologo(Codigo *c)
{
    Operando p0 = { OPD_PARAM, 0 };
    emite_byte(c, 0x55);
    emite_byte(c, 0x48); emite_byte(c, 0x89); emite_byte(c, 0xe5);
    emite_byte(c, 0x48); emite_byte(c, 0x83); emite_byte(c, 0xec);
    emite_byte(c, TAM_FRAME);
    emite_byte(c, 0x89); emite_byte(c, 0x7d);
    emite_byte(c, (unsigned char)offset_operando(p0));
}

/* leave; ret */
static void emite_epilogo(Codigo *c)
{
    emite_byte(c, 0xc9);
    emite_byte(c, 0xc3);
}

/* ------------------------------------------------------------------ */
/* Modulo 3: traducao de cada comando LBS                              */
/* ------------------------------------------------------------------ */

/* v<dst> = a op b */
static void traduz_oper(Codigo *c, int dst, Operando a, char op, Operando b)
{
    emite_carrega(c, REG_EAX, a);
    emite_carrega(c, REG_ECX, b);
    switch (op) {
    case '+': emite_byte(c, 0x01); emite_byte(c, 0xc8); break; /* add %ecx,%eax  */
    case '-': emite_byte(c, 0x29); emite_byte(c, 0xc8); break; /* sub %ecx,%eax  */
    case '*': emite_byte(c, 0x0f); emite_byte(c, 0xaf);
              emite_byte(c, 0xc1); break;                      /* imul %ecx,%eax */
    }
    emite_guarda_var(c, REG_EAX, dst);
}

/* v<dst> = call func arg */
static void traduz_call(Codigo *c, int dst, int func, Operando arg,
                        const int offsets[])
{
    int rel;
    emite_carrega(c, REG_EDI, arg);
    emite_byte(c, 0xe8);                              /* call rel32 */
    rel = offsets[func] - (c->pos + 4);
    emite_int32(c, rel);
    emite_guarda_var(c, REG_EAX, dst);
}

/* ret a */
static void traduz_ret(Codigo *c, Operando a)
{
    emite_carrega(c, REG_EAX, a);
    emite_epilogo(c);
}

/* zret a b: se a == 0 retorna b */
static void traduz_zret(Codigo *c, Operando a, Operando b)
{
    int pos_desvio;
    emite_carrega(c, REG_EAX, a);
    emite_byte(c, 0x83); emite_byte(c, 0xf8); emite_byte(c, 0x00); /* cmp $0,%eax */
    emite_byte(c, 0x75);                                           /* jne rel8 */
    pos_desvio = c->pos;
    emite_byte(c, 0x00);                                           /* placeholder */
    traduz_ret(c, b);
    c->buf[pos_desvio] = (unsigned char)(c->pos - (pos_desvio + 1));
}

/* ------------------------------------------------------------------ */
/* Modulo 4: parsing das linhas LBS                                    */
/* ------------------------------------------------------------------ */

static Operando parse_operando(const char *s)
{
    Operando op;
    op.valor = 0;
    switch (s[0]) {
    case 'v': op.tipo = OPD_VAR;   op.valor = atoi(s + 1); break;
    case 'p': op.tipo = OPD_PARAM; break;
    default:  op.tipo = OPD_CONST; op.valor = atoi(s + 1); break; /* '$' */
    }
    return op;
}

static void trata_ret(Codigo *c, const char *linha)
{
    char a[32];
    sscanf(linha, "ret %31s", a);
    traduz_ret(c, parse_operando(a));
}

static void trata_zret(Codigo *c, const char *linha)
{
    char a[32], b[32];
    sscanf(linha, "zret %31s %31s", a, b);
    traduz_zret(c, parse_operando(a), parse_operando(b));
}

static void trata_atribuicao(Codigo *c, const char *linha, const int offsets[])
{
    int dst;
    char t1[32], t2[32], t3[32];

    sscanf(linha, "v%d = %31s %31s %31s", &dst, t1, t2, t3);
    if (strcmp(t1, "call") == 0)
        traduz_call(c, dst, atoi(t2), parse_operando(t3), offsets);
    else
        traduz_oper(c, dst, parse_operando(t1), t2[0], parse_operando(t3));
}

/* ------------------------------------------------------------------ */
/* Funcao principal                                                    */
/* ------------------------------------------------------------------ */

void gera_codigo(FILE *f, unsigned char code[], funcp *entry)
{
    Codigo c;
    char linha[MAX_LINHA];
    int offsets[MAX_FUNCS];
    int nfuncs = 0;

    c.buf = code;
    c.pos = 0;
    *entry = NULL;

    while (fgets(linha, sizeof linha, f) != NULL) {
        if (strncmp(linha, "function", 8) == 0) {
            offsets[nfuncs++] = c.pos;
            emite_prologo(&c);
        } else if (strncmp(linha, "end", 3) == 0) {
            /* nada a emitir: o ret da funcao ja gera o epilogo */
        } else if (strncmp(linha, "ret", 3) == 0) {
            trata_ret(&c, linha);
        } else if (strncmp(linha, "zret", 4) == 0) {
            trata_zret(&c, linha);
        } else if (linha[0] == 'v') {
            trata_atribuicao(&c, linha, offsets);
        }
    }

    if (nfuncs > 0)
        *entry = (funcp)(code + offsets[nfuncs - 1]);
}