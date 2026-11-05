#ifndef ATENDENTE_H
#define ATENDENTE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>

/* Prioridade atribuida pela thread de Recepcao. */
typedef enum {
    PRIORIDADE_BAIXA = 0,
    PRIORIDADE_ALTA = 1
} Prioridade;

typedef struct
{
	long      N;                 // clientes a atender; 0 = infinitos
	long      X;                 // paciência, em ms, dos clientes de prioridade baixa
	FILA     *Fila;
	RECEPCAO *Recepcao;
	pid_t     PidAnalista;

	volatile pid_t PidEmAtendimento;  // 0 quando nenhum cliente está sendo atendido

	volatile int  Encerrar;      // pedido externo de parada (modo N = 0)
	volatile long Atendidos;
	volatile long Satisfeitos;

} ATENDENTE;

void Atendente(void *Argumento);
int  ClienteSatisfeito(CLIENTE *Cliente, long FimMs, long X);
void EsvaziaLng(ATENDENTE *Dados);
int  AnalistaAtivo();

#endif