#ifndef RECEPCAO_H
#define RECEPCAO_H

#include <semaphore.h>
#include "fila.h"

// a recepcao cria os processos cliente, sorteia a prioridade e poe na fila.
// tambem le N e X e cria os dois semaforos que o diagrama pede.
//
// no main fica assim (depois do escalonador_init):
//     long n, x;  FILA fila;  RECEPCAO r;
//     if (recepcao_le_parametros(argc, argv, &n, &x) != 0) return 1;
//     fila_inicia(&fila, x);
//     recepcao_inicia(&r, n, &fila);
//     thread_cria(recepcao_thread, &r, "recepcao", 1);

#define CLIENTE_EXEC   "./cliente"
#define DEMANDA_ARQ    "demanda.txt"
#define SEM_ATEND_NOME "/sem_atend"
#define SEM_BLOCK_NOME "/sem_block"

// se o cliente nao se suspender nesse tempo, algo deu errado
// (executavel errado, por exemplo) e a gente desiste
#define CLIENTE_TIMEOUT_MS 5000

typedef struct {
    long   n;                 // quantos clientes criar (0 = infinitos)
    FILA  *fila;

    // o atendente le esses campos pra saber como a recepcao ta
    volatile int  semaforos_prontos;
    volatile int  finalizada;   // 1 quando a recepcao terminou
    volatile int  erro;         // 1 se terminou por causa de uma falha
    volatile int  parar;        // vira 1 com recepcao_para()
    volatile long criados;      // quantos ja entraram na fila

    // ja abertos, quem precisar nao precisa dar sem_open de novo
    sem_t *sem_atend;
    sem_t *sem_block;

    unsigned long long semente; // estado do sorteador, uso interno
} RECEPCAO;

// le N e X de "./atendimento N X". 0 se ok, -1 (com mensagem) se invalido
int  recepcao_le_parametros(int argc, char *argv[], long *n, long *x);

void recepcao_inicia(RECEPCAO *r, long n, FILA *fila);

// corpo da thread. termina ao criar N clientes, com recepcao_para(),
// quando apertarem 's' ou se der erro
void recepcao_thread(void *arg);

void recepcao_para(RECEPCAO *r);

// apaga /sem_atend e /sem_block do sistema (chamar no fim do programa)
void recepcao_remove_semaforos(void);

// clientes que sobraram na fila ficam suspensos pra sempre, entao mata todos.
// devolve quantos matou
int  recepcao_descarta_fila(FILA *f);

#endif
