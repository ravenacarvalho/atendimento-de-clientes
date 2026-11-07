
#ifndef ATENDENTE_H
#define ATENDENTE_H

#include <sys/types.h>

#include "recepcao.h"
#include "fila.h"

typedef struct
{
    long N;                     // Número de clientes (0 = infinito)
    long X;                     // Paciência máxima em ms

    FILA *Fila;
    RECEPCAO *Recepcao;

    pid_t PidAnalista;
    volatile pid_t PidEmAtendimento;

    volatile int Encerrar;

    volatile long Atendidos;
    volatile long Satisfeitos;

} ATENDENTE;

// Corpo da thread do Atendente 
void Atendente(void *Argumento);

// Verifica se o cliente foi atendido dentro da paciência
int ClienteSatisfeito(CLIENTE *Cliente, long FimMs, long X);

// Processa os PIDs restantes do arquivo LNG
void EsvaziaLng(ATENDENTE *Dados);

// Verifica se o processo Analista está ativo
int AnalistaAtivo(void);

#endif
