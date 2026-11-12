#ifndef FILA_H
#define FILA_H

#include <sys/types.h>

// fila de clientes: a recepção coloca e o atendente tira
#define FILA_CAPACIDADE  100 // máximo de 100 esperando ao mesmo tempo

#define PRIORIDADE_BAIXA 0   // paciência X
#define PRIORIDADE_ALTA  1   // paciência X/2

// o enunciado pede pid, chegada, prioridade e tempo de atendimento
// adicionei prazo e ordem para escolher quem sai primeiro
typedef struct {
    pid_t         pid;
    int           prioridade;
    long          chegada_ms;         // hora em que o cliente foi criado
    long          tempo_atendimento;  // número que o cliente escreve no demanda.txt
    long          prazo_ms;           // chegada + paciência (calculado na inserção)
    unsigned long ordem;              // desempate quando o prazo é igual
} CLIENTE;

typedef struct {
    CLIENTE       itens[FILA_CAPACIDADE];
    int           n;                  // quantos clientes estão na fila
    long          x_ms;               // X da linha de comando
    unsigned long proxima_ordem;      // próxima senha de chegada
} FILA;

// zera a fila e guarda o X
void fila_inicia(FILA *f, long x_ms);

// paciência de cada prioridade (alta = X/2, baixa = X)
long fila_paciencia_ms(const FILA *f, int prioridade);

// 0 se inseriu, -1 se a fila está cheia.
// prazo e ordem não precisam vir preenchidos, a fila calcula
int fila_insere(FILA *f, const CLIENTE *c);

// tira o cliente de menor prazo. 0 se tirou, -1 se a fila está vazia
int fila_retira(FILA *f, CLIENTE *c);

int fila_tamanho(const FILA *f);
int fila_cheia(const FILA *f);
int fila_vazia(const FILA *f);

// satisfeito se (fim - chegada) <= paciência.
// o fim deve vir de tempo_agora_ms(), igual à chegada
int cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms);

#endif
