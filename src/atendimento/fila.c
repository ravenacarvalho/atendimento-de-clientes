#include <string.h>

#include "fila.h"
#include "escalonador.h"   // entra_atomico / sai_atomico

void fila_inicia(FILA *f, long x_ms)
{
    memset(f, 0, sizeof(*f));
    f->x_ms = x_ms;
}

long fila_paciencia_ms(const FILA *f, int prioridade)
{
    return (prioridade == PRIORIDADE_ALTA) ? f->x_ms / 2 : f->x_ms;
}

int fila_tamanho(const FILA *f) { return f->n; }
int fila_cheia(const FILA *f)   { return f->n >= FILA_CAPACIDADE; }
int fila_vazia(const FILA *f)   { return f->n == 0; }

int fila_insere(FILA *f, const CLIENTE *c)
{
    int ok = -1;

    // duas threads mexem nessa fila e o SIGALRM pode tirar a CPU a qualquer
    // momento, ate no meio desta funcao. entao bloqueio o sinal enquanto mexo
    // (nao usei mutex porque so existe uma thread do SO de verdade)
    entra_atomico();
    if (f->n < FILA_CAPACIDADE) {
        CLIENTE *novo = &f->itens[f->n];
        *novo = *c;

        // alta tem paciencia menor, entao ja nasce com prazo menor
        novo->prazo_ms = c->chegada_ms + fila_paciencia_ms(f, c->prioridade);
        novo->ordem    = f->proxima_ordem++;
        f->n++;
        ok = 0;
    }
    sai_atomico();
    return ok;
}

int fila_retira(FILA *f, CLIENTE *c)
{
    int i, melhor, ok = -1;

    entra_atomico();
    if (f->n > 0) {
        // politica: sai primeiro quem vai perder a paciencia antes (menor prazo).
        // se empatar, quem chegou primeiro. como tem no maximo 100, varrer
        // tudo e barato e mais simples do que montar um heap
        melhor = 0;
        for (i = 1; i < f->n; i++) {
            const CLIENTE *x = &f->itens[i];
            const CLIENTE *m = &f->itens[melhor];
            if (x->prazo_ms < m->prazo_ms ||
               (x->prazo_ms == m->prazo_ms && x->ordem < m->ordem))
                melhor = i;
        }
        *c = f->itens[melhor];

        // o ultimo vem pro lugar do que saiu. a ordem do vetor nao importa
        // porque eu sempre varro tudo pra escolher
        f->itens[melhor] = f->itens[f->n - 1];
        f->n--;
        ok = 0;
    }
    sai_atomico();
    return ok;
}

int cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms)
{
    // regra do enunciado (nota 6): tempo atual - chegada <= paciencia
    return (fim_ms - c->chegada_ms) <= fila_paciencia_ms(f, c->prioridade);
}
