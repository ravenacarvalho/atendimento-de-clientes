/*
 * analista.c
 *
 * Processo Analista:
 * - inicia e grava seu PID em ARQUIVO_PID_ANALISTA;
 * - permanece bloqueado aguardando SIGUSR1 enviado pelo Atendente;
 * - a cada despertar, imprime no maximo MAX_IMPRESSOS PIDs de ARQUIVO_LNG;
 * - remove do arquivo os PIDs que foram impressos;
 * - se ainda houver PIDs, agenda outro despertar para processar o proximo lote.
 *
 * Compile:
 *   gcc -Wall -Wextra -std=c11 analista.c -o analista -pthread
 *
 * O -pthread pode ser necessario em algumas distribuicoes para os semaforos
 * POSIX nomeados. Nao cria threads.
 */

#define _POSIX_C_SOURCE 200809L

#include "utilitarios.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static sem_t *sem_block = SEM_FAILED;

/* Aguarda o semaforo mesmo se a espera for interrompida por um sinal. */
static int aguardar_semaforo(sem_t *sem)
{
    int resultado;

    do {
        resultado = sem_wait(sem);
    } while (resultado == -1 && errno == EINTR);

    return resultado;
}

/* Grava o PID do Analista para que o Atendente possa encontra-lo. */
static int registrar_pid(void)
{
    char temporario[128];
    FILE *arquivo;

    if (snprintf(temporario, sizeof(temporario), "%s.tmp",
                 ARQUIVO_PID_ANALISTA) >= (int)sizeof(temporario)) {
        return -1;
    }

    arquivo = fopen(temporario, "w");
    if (arquivo == NULL) {
        perror("Analista: fopen PID temporario");
        return -1;
    }

    if (fprintf(arquivo, "%ld\n", (long)getpid()) < 0) {
        perror("Analista: fprintf PID");
        fclose(arquivo);
        unlink(temporario);
        return -1;
    }

    if (fclose(arquivo) != 0) {
        perror("Analista: fclose PID");
        unlink(temporario);
        return -1;
    }

    if (rename(temporario, ARQUIVO_PID_ANALISTA) != 0) {
        perror("Analista: rename PID");
        unlink(temporario);
        return -1;
    }

    return 0;
}

/*
 * Le e remove ate MAX_IMPRESSOS PIDs.
 * Retorna:
 *   1 se ainda restam PIDs no arquivo;
 *   0 se o arquivo ficou vazio;
 *  -1 em caso de erro.
 *
 * A leitura e a regravacao sao protegidas por SEM_BLOCK, compartilhado
 * com o processo Atendente que acrescenta PIDs ao arquivo LNG.
 */
static int processar_lote(void)
{
    FILE *entrada;
    FILE *saida;
    char linha[128];
    char temporario[128];
    long pid;
    char *fim;
    int impressos = 0;
    int existem_restantes = 0;

    if (aguardar_semaforo(sem_block) == -1) {
        perror("Analista: sem_wait");
        return -1;
    }

    entrada = fopen(ARQUIVO_LNG, "r");
    if (entrada == NULL) {
        if (errno == ENOENT) {
            sem_post(sem_block);
            return 0;
        }

        perror("Analista: fopen LNG");
        sem_post(sem_block);
        return -1;
    }

    if (snprintf(temporario, sizeof(temporario), "%s",
                 ARQUIVO_LNG_TEMP) >= (int)sizeof(temporario)) {
        fclose(entrada);
        sem_post(sem_block);
        return -1;
    }

    saida = fopen(temporario, "w");
    if (saida == NULL) {
        perror("Analista: fopen LNG temporario");
        fclose(entrada);
        sem_post(sem_block);
        return -1;
    }

    while (fgets(linha, sizeof(linha), entrada) != NULL) {
        errno = 0;
        pid = strtol(linha, &fim, 10);

        /* Linhas invalidas sao descartadas, evitando bloquear o arquivo. */
        if (fim == linha || errno == ERANGE) {
            continue;
        }

        if (impressos < MAX_IMPRESSOS) {
            printf("%ld\n", pid);
            impressos++;
        } else {
            /* Guarda este e todos os demais PIDs para o proximo lote. */
            if (fputs(linha, saida) == EOF) {
                perror("Analista: fputs");
                fclose(entrada);
                fclose(saida);
                unlink(temporario);
                sem_post(sem_block);
                return -1;
            }
            existem_restantes = 1;
        }
    }

    if (ferror(entrada)) {
        perror("Analista: leitura LNG");
        fclose(entrada);
        fclose(saida);
        unlink(temporario);
        sem_post(sem_block);
        return -1;
    }

    if (fclose(entrada) != 0) {
        perror("Analista: fclose LNG");
        fclose(saida);
        unlink(temporario);
        sem_post(sem_block);
        return -1;
    }

    if (fclose(saida) != 0) {
        perror("Analista: fclose temporario");
        unlink(temporario);
        sem_post(sem_block);
        return -1;
    }

    if (rename(temporario, ARQUIVO_LNG) != 0) {
        perror("Analista: rename LNG");
        unlink(temporario);
        sem_post(sem_block);
        return -1;
    }

    sem_post(sem_block);
    fflush(stdout);

    return existem_restantes;
}

/* Remove o arquivo de PID somente se ele ainda pertencer a este processo. */
static void remover_registro_pid(void)
{
    FILE *arquivo = fopen(ARQUIVO_PID_ANALISTA, "r");
    long pid_registrado;

    if (arquivo == NULL)
        return;

    if (fscanf(arquivo, "%ld", &pid_registrado) == 1 &&
        pid_registrado == (long)getpid()) {
        fclose(arquivo);
        unlink(ARQUIVO_PID_ANALISTA);
        return;
    }

    fclose(arquivo);
}


int main(void)
{
    sigset_t conjunto_sinais;
    int sinal_recebido;
    int resultado;
    int encerrar = 0;

    /*
        O Analista deve receber os sinais de forma sincronizada.
        SIGUSR1: processar os PIDs.
        SIGTERM: finalizar depois de esvaziar o arquivo.
    */
    sigemptyset(&conjunto_sinais);
    sigaddset(&conjunto_sinais, SIGUSR1);
    sigaddset(&conjunto_sinais, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &conjunto_sinais, NULL) == -1) {
        perror("Analista: sigprocmask");
        return EXIT_FAILURE;
    }

    sem_block = sem_open(SEM_BLOCK, O_CREAT, 0666, 1);

    if (sem_block == SEM_FAILED) {
        perror("Analista: sem_open SEM_BLOCK");
        return EXIT_FAILURE;
    }

    if (registrar_pid() == -1) {
        sem_close(sem_block);
        return EXIT_FAILURE;
    }

    printf("Analista iniciado (PID %ld), aguardando notificacoes...\n",
           (long)getpid());

    fflush(stdout);

    while (!encerrar) {

        if (sigwait(&conjunto_sinais, &sinal_recebido) != 0) {
            perror("Analista: sigwait");
            break;
        }

        if (sinal_recebido == SIGTERM)
            encerrar = 1;

       
        // Processa um lote de no maximo 10 PIDs.
        resultado = processar_lote();

        /*
          Se existem mais PIDs, continua processando.
          No encerramento, nao precisamos enviar sinais:
          podemos esvaziar todos os lotes antes de sair.
        */
        while (resultado > 0 && encerrar)
            resultado = processar_lote();

        if (resultado > 0 && !encerrar) {

            if (kill(getpid(), SIGUSR1) == -1)
                perror("Analista: reenviar SIGUSR1");
        }

        if (resultado < 0) {
            fprintf(stderr,
                    "Analista: falha ao processar lote.\n");
        }
    }

    // Remove o registro e fecha o semaforo.
    remover_registro_pid();

    sem_close(sem_block);

    printf("Analista encerrado.\n");

    return EXIT_SUCCESS;
}
