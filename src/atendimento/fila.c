#include <string.h>

#include "fila.h"
#include "escalonador.h"   // entra_atomico / sai_atomico

// Inicializa a fila vazia e o x (paciência) é guardado
void fila_inicia(FILA *f, long x_ms)
{
    memset(f, 0, sizeof(*f)); // zera tudo: n = 0 (fila vazia), contador de ordem = 0
    f->x_ms = x_ms; // guarda o X: paciência do cliente de prioridade baixa (alta usa X/2)
}

// Retorna quanto o cliente aguenta esperar
long fila_paciencia_ms(const FILA *f, int prioridade)
{
    // prioridade alta: X/2; prioridade baixa: X
    return (prioridade == PRIORIDADE_ALTA) ? f->x_ms / 2 : f->x_ms;
}

int fila_tamanho(const FILA *f) { return f->n; } // Devolve n, que são quantos clientes estão esperando
int fila_cheia(const FILA *f)   { return f->n >= FILA_CAPACIDADE; } // Se chegou ao limite da fila retorna 1 e senão 0
int fila_vazia(const FILA *f)   { return f->n == 0; } // Se fila vazia retorna 1 e senão 0

// Coloca um cliente na fila. Devolve 0 se inseriu, -1 se a fila está cheia
int fila_insere(FILA *f, const CLIENTE *c)
{
    int ok = -1; 

    entra_atomico(); // bloqueia o SIGALRM: bloqueia a troca de threads, ninguém interrompe enquanto a fila é alterada
    if (f->n < FILA_CAPACIDADE) {
        CLIENTE *novo = &f->itens[f->n]; // aponta para a primeira vaga livre (posição n)
        *novo = *c; // copia a ficha do cliente 

        // prazo = hora que chegou + quanto aguenta esperar
        novo->prazo_ms = c->chegada_ms + fila_paciencia_ms(f, c->prioridade);
        novo->ordem    = f->proxima_ordem++; // senha de chegada, só para desempate
        f->n++;
        ok = 0;
    }
    sai_atomico(); // desbloqueia o SIGALRM: a troca de threads volta a ser permitida
    return ok;
}

// Retira da fila o cliente de menor prazo. Devolve 0 se retirou, -1 se a fila está vazia
int fila_retira(FILA *f, CLIENTE *c)
{
    int i, melhor, ok = -1; // melhor: posição do cliente de menor prazo

    entra_atomico(); // bloqueia a troca de threads
    if (f->n > 0) { 
        // varre a fila toda atrás do menor prazo; se empatar, quem chegou antes
        melhor = 0;
        for (i = 1; i < f->n; i++) {
            const CLIENTE *x = &f->itens[i]; // candidato da vez
            const CLIENTE *m = &f->itens[melhor]; // campeão atual
            /* candidato vira campeão se tem prazo menor, ou prazo igual e chegou antes */
            if (x->prazo_ms < m->prazo_ms ||
               (x->prazo_ms == m->prazo_ms && x->ordem < m->ordem))
                melhor = i; // Guarda a posição do novo campeão
        }
        *c = f->itens[melhor]; // copia o cliente escolhido para c

        // o último ocupa o lugar de quem saiu (a ordem do vetor não importa,
        // pois toda retirada varre a fila inteira de novo)
        f->itens[melhor] = f->itens[f->n - 1];
        f->n--; // Diminui o núm de clientes
        ok = 0; // Marca sucesso
    }
    sai_atomico(); // desbloqueia a troca de threads
    return ok;
}

// Devolve 1 se o cliente foi atendido dentro da paciência dele, 0 se não.
int cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms)
{
    // Regra do enunciado: (hora do fim - hora de chegada) <= paciência
    return (fim_ms - c->chegada_ms) <= fila_paciencia_ms(f, c->prioridade);
}
