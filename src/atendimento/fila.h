#ifndef FILA_H
#define FILA_H

#include <sys/types.h>

// fila dos clientes que a recepcao cria. a recepcao insere e o atendente tira.
// o enunciado limita em 100 esperando ao mesmo tempo (nota 9 do diagrama)
#define FILA_CAPACIDADE  100

#define PRIORIDADE_BAIXA 0   // paciencia X
#define PRIORIDADE_ALTA  1   // paciencia X/2

// o diagrama pede pid, hora de chegada, prioridade e tempo de atendimento.
// acrescentei o prazo e a ordem, que servem pra decidir quem sai primeiro
typedef struct {
    pid_t         pid;
    int           prioridade;
    long          chegada_ms;         // tempo_agora_ms() quando o cliente foi criado
    long          tempo_atendimento;  // o numero que o cliente escreveu no demanda.txt
    long          prazo_ms;           // chegada + paciencia (calculado na insercao)
    unsigned long ordem;              // so pra desempatar quem tem o mesmo prazo
} CLIENTE;

typedef struct {
    CLIENTE       itens[FILA_CAPACIDADE];
    int           n;
    long          x_ms;               // o X que veio da linha de comando
    unsigned long proxima_ordem;
} FILA;

// deixa a fila vazia e guarda o X
void fila_inicia(FILA *f, long x_ms);

// paciencia em ms pra cada prioridade (alta = X/2, baixa = X)
long fila_paciencia_ms(const FILA *f, int prioridade);

// 0 se inseriu, -1 se ta cheia. quem chama nao precisa preencher prazo nem ordem
int fila_insere(FILA *f, const CLIENTE *c);

// tira o cliente de menor prazo. 0 se deu certo, -1 se a fila ta vazia
int fila_retira(FILA *f, CLIENTE *c);

int fila_tamanho(const FILA *f);
int fila_cheia(const FILA *f);
int fila_vazia(const FILA *f);

// satisfeito se (fim - chegada) <= paciencia. o fim tem que vir de
// tempo_agora_ms(), a mesma funcao que usei pra marcar a chegada
int cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms);

#endif
