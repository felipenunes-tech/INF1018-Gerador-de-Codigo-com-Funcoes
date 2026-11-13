#include <stdio.h>
#include <stdlib.h>
#include "gera_codigo.h"

/* uso: ./teste arquivo.lbs argumento */
int main(int argc, char *argv[])
{
    FILE *fp;
    unsigned char code[500];
    funcp funcLBS;
    int arg, res;

    if (argc < 3) {
        fprintf(stderr, "uso: %s arquivo.lbs argumento\n", argv[0]);
        return 1;
    }
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }
    gera_codigo(fp, code, &funcLBS);
    fclose(fp);
    if (funcLBS == NULL) {
        printf("Erro na geracao\n");
        return 1;
    }
    arg = atoi(argv[2]);
    res = (*funcLBS)(arg);
    printf("%d\n", res);
    return 0;
}