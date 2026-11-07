
#define _POSIX_C_SOURCE 200809L

#include "atendente.h"

#include "escalonador.h"
#include "utilitarios.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <semaphore.h>

/* ============================================================
   FUNCOES AUXILIARES
   ============================================================ */

// Recupera o PID do processo Analista.
static pid_t obter_pid_analista(void)
{
    char buffer[64];
    ssize_t tamanho;
    int arquivo;

    arquivo = open(ARQUIVO_PID_ANALISTA, O_RDONLY);

    if (arquivo < 0)
        return -1;

    tamanho = read(arquivo, buffer, sizeof(buffer) - 1);

    close(arquivo);

    if (tamanho <= 0)
        return -1;

    buffer[tamanho] = '\0';

    return (pid_t)strtol(buffer, NULL, 10);
}


// Aguarda um semaforo sem bloquear o processo inteiro.
static int aguardar_semaforo(sem_t *semaforo)
{
    while (sem_trywait(semaforo) == -1)
    {
        if (errno != EAGAIN && errno != EINTR)
            return -1;

        cede_processador();
    }

    return 0;
}


/* ============================================================
   VERIFICACAO DO ANALISTA
   ============================================================ */

int AnalistaAtivo(void)
{
    pid_t pid = obter_pid_analista();

    if (pid <= 0)
        return 0;

    if (kill(pid, 0) == 0 || errno == EPERM)
        return 1;

    return 0;
}


/* ============================================================
   CALCULO DA SATISFACAO
   ============================================================ */

int ClienteSatisfeito(CLIENTE *Cliente, long FimMs, long X)
{
    long paciencia;

    if (Cliente == NULL)
        return 0;

    if (Cliente->prioridade == PRIORIDADE_ALTA)
        paciencia = X / 2;
    else
        paciencia = X;

    return (FimMs - Cliente->chegada_ms) <= paciencia;
}


/* ============================================================
   ESPERA PELO ATENDIMENTO DO CLIENTE
   ============================================================ */

/*
  O cliente foi criado suspenso com SIGSTOP.
 
  O Atendente envia SIGCONT e aguarda o término do processo.
 
  O semáforo /sem_atend é utilizado pelo cliente para sinalizar
  a execução do atendimento.
 
  Não usamos waitpid bloqueante.
*/

static int aguarda_cliente(CLIENTE *Cliente, sem_t *SemAtend)
{
    int status;
    int terminou = 0;
    int valor;

    if (kill(Cliente->pid, SIGCONT) == -1)
    {
        perror("Atendente: SIGCONT");
        return -1;
    }

    while (1)
    {
        if (!terminou)
        {
            pid_t resultado = waitpid(
                Cliente->pid,
                &status,
                WNOHANG
            );

            if (resultado == Cliente->pid)
                terminou = 1;

            else if (resultado == -1 && errno != EINTR)
            {
                perror("Atendente: waitpid");
                return -1;
            }
        }

        /*
          Só consideramos o atendimento concluído quando:
          1. O processo Cliente terminou.
          2. O semáforo foi liberado pelo Cliente.
        */
        if (terminou)
        {
            if (sem_getvalue(SemAtend, &valor) == 0 && valor > 0)
                return 0;
        }

        cede_processador();
    }
}


/* ============================================================
   REGISTRO DO PID NO ARQUIVO LNG
   ============================================================ */

static int registra_pid(pid_t pid, sem_t *SemBlock)
{
    char buffer[64];
    int arquivo;
    int tamanho;
    int resultado = -1;

    if (aguardar_semaforo(SemBlock) != 0)
    {
        perror("Atendente: semaforo de bloqueio");
        return -1;
    }

    arquivo = open(
        ARQUIVO_LNG,
        O_WRONLY | O_CREAT | O_APPEND,
        0666
    );

    if (arquivo < 0)
    {
        perror("Atendente: abrir LNG");
        sem_post(SemBlock);
        return -1;
    }

    tamanho = snprintf(
        buffer,
        sizeof(buffer),
        "%ld\n",
        (long)pid
    );

    if (tamanho > 0)
    {
        ssize_t escritos = write(arquivo, buffer, (size_t)tamanho);

        if (escritos == tamanho)
            resultado = 0;
    }

    if (resultado != 0)
        perror("Atendente: escrever LNG");

    close(arquivo);

    sem_post(SemBlock);

    return resultado;
}


/* ============================================================
   NOTIFICACAO DO ANALISTA
   ============================================================ */

static void acorda_analista(ATENDENTE *Dados)
{
    pid_t pid = Dados->PidAnalista;

    if (pid <= 0)
    {
        pid = obter_pid_analista();
        Dados->PidAnalista = pid;
    }

    if (pid > 0)
    {
        if (kill(pid, SIGUSR1) == -1 && errno != ESRCH)
            perror("Atendente: acordar Analista");
    }
}


/* ============================================================
   VERIFICACAO DO ARQUIVO LNG
   ============================================================ */

/* Retorna 1 se existem dados, 0 se vazio e -1 em caso de erro. */
static int lng_possui_dados(sem_t *SemBlock)
{
    char caractere;
    ssize_t leitura;
    int arquivo;

    if (aguardar_semaforo(SemBlock) != 0)
        return -1;

    arquivo = open(ARQUIVO_LNG, O_RDONLY);

    if (arquivo < 0)
    {
        int erro = errno;

        sem_post(SemBlock);

        if (erro == ENOENT)
            return 0;

        return -1;
    }

    leitura = read(arquivo, &caractere, 1);

    close(arquivo);
    sem_post(SemBlock);

    return leitura > 0 ? 1 : 0;
}


/* ============================================================
   ESVAZIAMENTO DO LNG
   ============================================================ */

/*
  Utilizado principalmente quando N = 0.
 
  O Analista processa no maximo 10 PIDs por despertar.
  Por isso, notificamos novamente enquanto houver dados.
*/

void EsvaziaLng(ATENDENTE *Dados)
{
    int resultado;

    if (Dados == NULL || Dados->Recepcao == NULL)
        return;

    while (1)
    {
        resultado = lng_possui_dados(Dados->Recepcao->sem_block);

        if (resultado <= 0)
            break;

        acorda_analista(Dados);

        thread_dorme(1);
    }

    /*
      Dá oportunidade para o Analista concluir a impressão
      do último lote, inclusive quando o arquivo já ficou vazio.
    */
    if (AnalistaAtivo())
        thread_dorme(10);
}


/* ============================================================
   THREAD DO ATENDENTE
   ============================================================ */

void Atendente(void *Argumento)
{
    ATENDENTE *Dados = (ATENDENTE *)Argumento;

    CLIENTE Cliente;

    RECEPCAO *Recepcao;

    sem_t *SemAtend;
    sem_t *SemBlock;

    long fim_ms;

    if (Dados == NULL)
        return;

    Recepcao = Dados->Recepcao;

    if (Recepcao == NULL || Dados->Fila == NULL)
        return;

    /*
      Aguarda a Recepção criar os semáforos.
    */
    while (!Recepcao->semaforos_prontos)
    {
        if (Recepcao->finalizada && Recepcao->erro)
            return;

        cede_processador();
    }

    SemAtend = Recepcao->sem_atend;
    SemBlock = Recepcao->sem_block;

    /*
      Guarda o PID do Analista quando ele já estiver disponível.
    */
    Dados->PidAnalista = obter_pid_analista();

    Dados->Atendidos = 0;
    Dados->Satisfeitos = 0;

    Dados->PidEmAtendimento = 0;
    Dados->Encerrar = 0;

    while (1)
    {
        /*
            Modo infinito:
            quando o usuário pressiona 's', termina o atendimento
            atual e não inicia outro.
        */
        if (Dados->N == 0 && escalonador_deve_parar())
        {
            Dados->Encerrar = 1;
            break;
        }

        /*
            Tenta retirar um cliente da fila.
        */
        if (fila_retira(Dados->Fila, &Cliente) != 0)
        {
            /*
                Se a Recepção terminou e não existem mais clientes,
                o trabalho do Atendente também terminou.
            */
            if (Recepcao->finalizada &&
                fila_vazia(Dados->Fila))
            {
                break;
            }

            /*
                Com N finito, aguardamos a Recepção inserir
                os próximos clientes.
            */
            thread_dorme(1);
            continue;
        }

        Dados->PidEmAtendimento = Cliente.pid;

        /*
            Acorda o cliente e aguarda o término do atendimento.
        */
        if (aguarda_cliente(&Cliente, SemAtend) != 0)
        {
            Dados->PidEmAtendimento = 0;
            continue;
        }

        /*
            O processo terminou. Recolhemos seu status com waitpid
            e registramos o PID no arquivo LNG.
            O waitpid usado em aguarda_cliente já recolheu o processo.
        */

        if (registra_pid(Cliente.pid, SemBlock) == 0)
        {
            /*
                O Analista é acordado após o registro.
            */
            acorda_analista(Dados);
        }

        /*
            Calcula a satisfação utilizando o mesmo relógio
            monotônico empregado pela Recepção.
         */
        fim_ms = tempo_agora_ms();

        entra_atomico();

        Dados->Atendidos++;

        if (ClienteSatisfeito(&Cliente, fim_ms, Dados->X))
            Dados->Satisfeitos++;

        Dados->PidEmAtendimento = 0;

        sai_atomico();
    }

    /*
        No modo infinito, garante que os PIDs já atendidos
        sejam encaminhados ao Analista antes do encerramento.
    */
    if (Dados->N == 0)
        EsvaziaLng(Dados);

    Dados->Encerrar = 1;
}
