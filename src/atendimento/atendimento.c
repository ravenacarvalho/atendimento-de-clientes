#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <semaphore.h>

#include "atendente.h"
#include "recepcao.h"
#include "fila.h"
#include "escalonador.h"
#include "encerramento.h"
#include "utilitarios.h"

/* ============================================================
   INICIALIZACAO DO ANALISTA
   ============================================================ 
*/

/*
    Cria o processo Analista.
    O processo filho executa o programa analista independente
    do escalonador de threads.
 */
static pid_t inicia_analista(void)
{
    pid_t pid;

    pid = fork();

    if (pid < 0) {
        perror("Atendimento: fork do Analista");
        return -1;
    }

    if (pid == 0) {

        execl("./analista", "analista", (char *)NULL);

        perror("Atendimento: exec do Analista");

        _exit(EXIT_FAILURE);
    }

    return pid;
}


/*
    Espera o Analista registrar seu PID.
    Isso garante que ele ja abriu o semaforo /sem_block
    antes de a Recepcao e o Atendente comecarem a trabalhar.
*/
static int aguarda_analista(pid_t pid)
{
    struct timespec espera = {
        .tv_sec = 0,
        .tv_nsec = 10000000
    };

    long tentativas;

    for (tentativas = 0; tentativas < 500; tentativas++) {

        FILE *arquivo;
        long pid_registrado;

        arquivo = fopen(ARQUIVO_PID_ANALISTA, "r");

        if (arquivo != NULL) {

            if (fscanf(arquivo, "%ld", &pid_registrado) == 1) {

                fclose(arquivo);

                if (pid_registrado == (long)pid)
                    return 0;

            } else {
                fclose(arquivo);
            }
        }

        // Verifica se o processo ainda esta vivo.
        if (waitpid(pid, NULL, WNOHANG) == pid) {
            fprintf(stderr,
                    "Atendimento: o Analista terminou inesperadamente.\n");
            return -1;
        }

        nanosleep(&espera, NULL);
    }

    fprintf(stderr,
            "Atendimento: tempo esgotado aguardando o Analista.\n");

    return -1;
}


/* ============================================================
   ENCERRAMENTO DO ANALISTA
   ============================================================ 
*/

static void encerra_analista(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;

    /*
        Solicita que o Analista esvazie o arquivo LNG
        e finalize sua execucao.
    */
    if (kill(pid, SIGTERM) == -1) {

        if (errno != ESRCH)
            perror("Atendimento: SIGTERM no Analista");
    }

    // Aguarda o encerramento do processo filho.
    while (waitpid(pid, &status, 0) == -1) {

        if (errno == EINTR)
            continue;

        if (errno != ECHILD)
            perror("Atendimento: waitpid do Analista");

        break;
    }
}


/* ============================================================
   PROGRAMA PRINCIPAL
   ============================================================ 
*/

int main(int argc, char *argv[])
{
    long N;
    long X;

    FILA fila;

    RECEPCAO recepcao;

    ATENDENTE atendente;

    pid_t pid_analista = -1;

    int recepcao_criada;
    int atendente_criado;

    /*
        1. Leitura dos parametros.
        Uso: ./atendimento N X
    */
    if (recepcao_le_parametros(argc, argv, &N, &X) != 0)
        return EXIT_FAILURE;

    printf("====================================\n");
    printf("       SISTEMA DE ATENDIMENTO\n");
    printf("====================================\n");

    printf("Clientes: ");

    if (N == 0)
        printf("infinitos\n");
    else
        printf("%ld\n", N);

    printf("Paciencia maxima: %ld ms\n", X);

    if (N == 0)
        printf("Pressione 's' para encerrar.\n");

    printf("\n");

    // 2. Limpeza de recursos de uma execucao anterior.
    unlink(ARQUIVO_LNG);
    unlink(ARQUIVO_LNG_TEMP);
    unlink(ARQUIVO_PID_ANALISTA);

    recepcao_remove_semaforos();

    // 3. Inicializacao da fila e da Recepcao.
    fila_inicia(&fila, X);

    recepcao_inicia(&recepcao, N, &fila);

    // 4. Inicializacao dos dados do Atendente.
    memset(&atendente, 0, sizeof(atendente));

    atendente.N = N;
    atendente.X = X;
    atendente.Fila = &fila;
    atendente.Recepcao = &recepcao;

    // 5. Inicializacao do escalonador.
    escalonador_init();

    // 6. Criacao do processo Analista.
    pid_analista = inicia_analista();

    if (pid_analista < 0)
        return EXIT_FAILURE;

    // Aguarda o registro do PID e a abertura do semaforo.
    if (aguarda_analista(pid_analista) != 0) {
        encerra_analista(pid_analista);
        recepcao_remove_semaforos();
        return EXIT_FAILURE;
    }

    atendente.PidAnalista = pid_analista;

    // 7. Inicio da medicao do tempo total.
    encerramento_inicia();

    /*
      8. Criacao das threads. A Recepcao cria os clientes.
      O Atendente retira os clientes da fila.
    */
    recepcao_criada = thread_cria(
        recepcao_thread,
        &recepcao,
        "recepcao",
        1
    );

    if (recepcao_criada < 0) {

        fprintf(stderr, "Atendimento: falha ao criar thread Recepcao.\n");

        encerra_analista(pid_analista);

        recepcao_remove_semaforos();

        return EXIT_FAILURE;
    }

    atendente_criado = thread_cria(
        Atendente,
        &atendente,
        "atendente",
        2
    );

    if (atendente_criado < 0) {

        fprintf(stderr, "Atendimento: falha ao criar thread Atendente.\n");

        recepcao_para(&recepcao);

        escalonador_executa();

        encerra_analista(pid_analista);

        recepcao_remove_semaforos();

        return EXIT_FAILURE;
    }

    /*
        9. Execucao do escalonador.
        O escalonador retorna quando todas as threads terminarem.
    */
    printf("Atendimento iniciado!\n\n");

    fflush(stdout);

    escalonador_executa();

    /*
        10. Descarta os clientes que permaneceram na fila.
        Isso acontece principalmente no modo infinito, quando o usuario solicita o encerramento.
    */
    recepcao_descarta_fila(&fila);

    /*
        11. Solicita o encerramento do Analista.
        O Analista ainda processara os PIDs restantes no LNG.
    */
    encerra_analista(pid_analista);

    // 12. Apresentacao dos resultados.
    encerramento_relatorio(
        atendente.Atendidos,
        atendente.Satisfeitos
    );

    // 13. Limpeza dos recursos.
    if (recepcao.sem_atend != SEM_FAILED &&
        recepcao.sem_atend != NULL)
        sem_close(recepcao.sem_atend);

    if (recepcao.sem_block != SEM_FAILED &&
        recepcao.sem_block != NULL)
        sem_close(recepcao.sem_block);

    recepcao_remove_semaforos();

    unlink(ARQUIVO_LNG);
    unlink(ARQUIVO_LNG_TEMP);
    unlink(ARQUIVO_PID_ANALISTA);
    unlink(ARQUIVO_DEMANDA);

    printf("\nAtendimento finalizado.\n");

    return EXIT_SUCCESS;
}
