#define _GNU_SOURCE
#include <stdio.h>

#include "encerramento.h"
#include "escalonador.h"   /* so para usar tempo_agora_ms() */

static long inicio_ms = 0;

void encerramento_inicia(void)
{
    inicio_ms = tempo_agora_ms();
}

long encerramento_tempo_total_ms(void)
{
    if (inicio_ms == 0) return 0;      /* ninguem chamou o inicia()  */
    return tempo_agora_ms() - inicio_ms;
}

double encerramento_taxa(long atendidos, long satisfeitos)
{
    /* numeros negativos nao fazem sentido aqui, viram zero */
    if (atendidos   < 0) atendidos   = 0;
    if (satisfeitos < 0) satisfeitos = 0;

    /* ninguem atendido: taxa zero, e nao uma divisao por zero */
    if (atendidos == 0) return 0.0;

    /* mais satisfeitos do que atendidos seria erro de contagem de
       quem chamou; o maximo que devolvemos e 100%                  */
    if (satisfeitos > atendidos) satisfeitos = atendidos;

    return (100.0 * (double)satisfeitos) / (double)atendidos;
}

void encerramento_relatorio(long atendidos, long satisfeitos)
{
    printf("\n=== Resultado ===\n");
    printf("clientes atendidos  : %ld\n", atendidos);
    printf("clientes satisfeitos: %ld\n", satisfeitos);
    printf("taxa de satisfacao  : %.2f%%\n",
           encerramento_taxa(atendidos, satisfeitos));
    printf("tempo total         : %ld ms\n", encerramento_tempo_total_ms());
    fflush(stdout);
}
