#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <semaphore.h>

#include "recepcao.h"
// do escalonador: cede_processador, thread_dorme, tempo_agora_ms,
// entra_atomico/sai_atomico e escalonador_deve_parar
#include "escalonador.h"

// imprime erro em stderr. o fprintf tem um lock interno, entao se a thread
// perder a CPU no meio e outra tentar imprimir, trava. por isso fica atomico
static void avisa(const char *fmt, ...)
{
    va_list ap;

    entra_atomico();
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    sai_atomico();
}

// sorteador proprio (xorshift). nao usei rand() porque ele guarda estado
// global com lock, e com preempcao eu prefiro nao depender disso
static unsigned prox_bit(RECEPCAO *r)
{
    unsigned long long x = r->semente;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->semente = x;
    return (unsigned)((x * 2685821657736338717ULL) >> 63);
}

// parar pode vir de dois lugares: recepcao_para() ou a tecla 's' (que o
// escalonador trata)
static int deve_parar(const RECEPCAO *r)
{
    // uma alternativa caso dê erro
    // return r->parar || (r->n == 0 && escalonador_deve_parar());
    return r->parar || escalonador_deve_parar();
}

// ---- leitura de N e X ----

static int le_inteiro(const char *s, long *out)
{
    char *fim;
    long v;

    if (s == NULL || *s == '\0') return -1;
    errno = 0;
    v = strtol(s, &fim, 10);
    if (errno != 0 || *fim != '\0') return -1;   // "10x" ou numero gigante
    *out = v;
    return 0;
}

int recepcao_le_parametros(int argc, char *argv[], long *n, long *x)
{
    long vn, vx;
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "atendimento";

    if (argc != 3 || le_inteiro(argv[1], &vn) != 0
                  || le_inteiro(argv[2], &vx) != 0) {
        fprintf(stderr, "uso: %s N X\n"
                        "  N: numero de clientes (0 = infinitos)\n"
                        "  X: paciencia maxima em ms (prioridade baixa)\n",
                prog);
        return -1;
    }
    if (vn < 0) {
        fprintf(stderr, "%s: N deve ser >= 0 (recebi %ld)\n", prog, vn);
        return -1;
    }
    if (vx < 1) {
        fprintf(stderr, "%s: X deve ser >= 1 ms (recebi %ld)\n", prog, vx);
        return -1;
    }
    *n = vn;
    *x = vx;
    return 0;
}

// ---- semaforos ----

void recepcao_remove_semaforos(void)
{
    sem_unlink(SEM_ATEND_NOME);
    sem_unlink(SEM_BLOCK_NOME);
}

static int cria_semaforos(RECEPCAO *r)
{
    // se uma execucao anterior caiu, pode ter sobrado semaforo velho no
    // sistema. um com valor 0 travaria tudo, entao apaga antes de criar
    recepcao_remove_semaforos();

    r->sem_atend = sem_open(SEM_ATEND_NOME, O_CREAT, 0644, 1);
    if (r->sem_atend == SEM_FAILED) {
        avisa("recepcao: sem_open(%s): %s\n", SEM_ATEND_NOME, strerror(errno));
        return -1;
    }
    r->sem_block = sem_open(SEM_BLOCK_NOME, O_CREAT, 0644, 1);
    if (r->sem_block == SEM_FAILED) {
        avisa("recepcao: sem_open(%s): %s\n", SEM_BLOCK_NOME, strerror(errno));
        sem_close(r->sem_atend);
        recepcao_remove_semaforos();
        return -1;
    }
    return 0;
}

// ---- criacao de um cliente ----

// fork + exec do cliente. devolve o pid ou -1.
// o fork fica em secao atomica: se a thread perdesse a CPU no meio dele,
// algum lock interno do libc (malloc, por exemplo) podia ficar preso
static pid_t cria_processo_cliente(void)
{
    pid_t pid;

    entra_atomico();
    pid = fork();
    if (pid == 0) {
        // o filho herda a mascara com SIGALRM bloqueado. limpo antes do exec
        sigset_t vazio;
        sigemptyset(&vazio);
        sigprocmask(SIG_SETMASK, &vazio, NULL);
        execl(CLIENTE_EXEC, "cliente", (char *)NULL);
        _exit(127);   // so chega aqui se o exec falhou
    }
    sai_atomico();

    if (pid < 0)
        avisa("recepcao: fork: %s\n", strerror(errno));
    return pid;
}

static void mata_cliente(pid_t pid)
{
    int status;
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);   // depois do SIGKILL volta rapido
}

// o cliente escreve o demanda.txt e se suspende com SIGSTOP. a recepcao so
// pode ler o arquivo depois disso.
//
// aqui nao da pra usar waitpid bloqueante: as threads sao de usuario, entao
// ele pararia o processo inteiro e o atendente ficaria congelado junto.
// uso WNOHANG e devolvo a CPU pro escalonador entre as tentativas.
//
// devolve 0 (cliente suspenso), -1 (erro) ou 1 (mandaram parar, ja matei o cliente)
static int espera_cliente_suspender(const RECEPCAO *r, pid_t pid)
{
    int status;
    long limite = tempo_agora_ms() + CLIENTE_TIMEOUT_MS;

    for (;;) {
        pid_t w = waitpid(pid, &status, WUNTRACED | WNOHANG);

        if (w == pid) {
            if (WIFSTOPPED(status)) return 0;
            avisa("recepcao: o cliente terminou sem se suspender "
                  "(exec de %s falhou?)\n", CLIENTE_EXEC);
            return -1;
        }
        if (w < 0 && errno != EINTR) {
            avisa("recepcao: waitpid: %s\n", strerror(errno));
            return -1;
        }
        if (deve_parar(r)) {
            mata_cliente(pid);
            return 1;
        }
        if (tempo_agora_ms() > limite) {
            avisa("recepcao: cliente nao se suspendeu em %d ms\n",
                  CLIENTE_TIMEOUT_MS);
            mata_cliente(pid);
            return -1;
        }
        cede_processador();
    }
}

// le o numero que o cliente escreveu e apaga o arquivo. uso open/read em vez
// de fopen porque o fopen faz malloc, e eu nao quero malloc numa thread que
// pode ser interrompida
static int le_demanda(long *tempo)
{
    char buf[32];
    ssize_t k;
    int fd = open(DEMANDA_ARQ, O_RDONLY);

    if (fd < 0) {
        avisa("recepcao: abrir %s: %s\n", DEMANDA_ARQ, strerror(errno));
        return -1;
    }
    k = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    unlink(DEMANDA_ARQ);

    if (k <= 0) return -1;
    buf[k] = '\0';
    return le_inteiro(buf, tempo);
}

// cria um cliente e poe na fila. devolve 0 (inseriu), 1 (mandaram parar) ou -1 (erro)
static int cria_cliente(RECEPCAO *r)
{
    CLIENTE c;
    long tempo;
    int  esp;
    pid_t pid;

    pid = cria_processo_cliente();
    if (pid < 0) return -1;

    // o enunciado (nota 4) diz que a chegada e o momento da criacao do cliente
    memset(&c, 0, sizeof(c));
    c.pid        = pid;
    c.chegada_ms = tempo_agora_ms();

    esp = espera_cliente_suspender(r, pid);
    if (esp != 0) return esp;

    if (le_demanda(&tempo) != 0) {
        avisa("recepcao: demanda invalida do cliente %d\n", (int)pid);
        mata_cliente(pid);
        return -1;
    }
    c.tempo_atendimento = tempo;
    c.prioridade = prox_bit(r) ? PRIORIDADE_ALTA : PRIORIDADE_BAIXA;   // 50% cada

    if (fila_insere(r->fila, &c) != 0) {   // nao deveria acontecer, a thread ja checa antes
        avisa("recepcao: fila cheia na insercao\n");
        mata_cliente(pid);
        return -1;
    }
    r->criados++;
    return 0;
}

// ---- a thread ----

void recepcao_inicia(RECEPCAO *r, long n, FILA *fila)
{
    struct timespec ts;

    memset(r, 0, sizeof(*r));
    r->n    = n;
    r->fila = fila;

    // semente do sorteador: mistura hora e pid pra nao dar o mesmo toda vez
    clock_gettime(CLOCK_REALTIME, &ts);
    r->semente = ((unsigned long long)ts.tv_nsec << 20)
               ^ ((unsigned long long)ts.tv_sec  << 7)
               ^ (unsigned long long)getpid()
               ^ 0x9E3779B97F4A7C15ULL;   // constante so pra nunca ser zero
}

void recepcao_para(RECEPCAO *r)
{
    r->parar = 1;
}

void recepcao_thread(void *arg)
{
    RECEPCAO *r = (RECEPCAO *)arg;

    if (cria_semaforos(r) != 0) {
        r->erro = 1;
        r->finalizada = 1;
        return;
    }
    r->semaforos_prontos = 1;

    // com N = 0 o laco nunca acaba sozinho, so por parada ou erro
    while (r->n == 0 || r->criados < r->n) {
        int res;

        if (deve_parar(r)) break;

        // fila com 100: espera abrir vaga (nota 9). so a recepcao insere,
        // entao a fila nao enche entre esse teste e a insercao.
        // durmo 1 ms em vez de ficar rodando o laco sem parar
        if (fila_cheia(r->fila)) {
            thread_dorme(1);
            continue;
        }

        res = cria_cliente(r);
        if (res < 0) { r->erro = 1; break; }
        if (res > 0) break;   // mandaram parar no meio da criacao
    }
    r->finalizada = 1;
}

// ---- limpeza no fim ----

int recepcao_descarta_fila(FILA *f)
{
    CLIENTE c;
    int k = 0;

    while (fila_retira(f, &c) == 0) {
        mata_cliente(c.pid);
        k++;
    }
    return k;
}
