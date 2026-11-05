#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>

#include "escalonador.h"
#include "teclado.h"

/* -------------------------------------------------------------------
   ESTADO GLOBAL
   ------------------------------------------------------------------- */
static tcb_t      threads[MAX_THREADS];
static int        n_threads = 0;
static int        atual = -1;          /* thread rodando (-1 = nenhuma) */
static ucontext_t ctx_escalonador;     /* contexto central              */
static long       n_trocas = 0;
static long       n_preempcoes = 0;
static int        nivel_main = 0;      /* secoes atomicas fora de thread */

/* 'volatile' porque o valor muda fora do fluxo normal (tratador de
   sinal e trocas de contexto). Sem isso o compilador poderia achar
   que o valor nunca muda e nunca sair de um laco de espera.         */
static volatile int parar = 0;


/* ===================================================================
   1) TEMPO
   =================================================================== */
long tempo_agora_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000L) + (ts.tv_nsec / 1000000L);
}

/* Pausa real do processo. SO o escalonador usa, e so quando nenhuma
   thread esta pronta (todas dormindo). As threads usam thread_dorme. */
static void escalonador_cochila(int ms)
{
    struct timespec t;
    t.tv_sec  = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}


/* ===================================================================
   2) SECAO ATOMICA (bloquear / desbloquear a preempcao)
   =================================================================== */

/* Bloquear o SIGALRM impede o temporizador de tirar a CPU da thread.
   E o equivalente, em espaco de usuario, a desabilitar interrupcoes. */
static void bloqueia_alarme(sigset_t *mascara_antiga)
{
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGALRM);
    sigprocmask(SIG_BLOCK, &m, mascara_antiga);
}

static void restaura_mascara(const sigset_t *mascara_antiga)
{
    sigprocmask(SIG_SETMASK, mascara_antiga, NULL);
}

/* O contador de aninhamento e POR THREAD, guardado no TCB. Se fosse
   uma variavel so para todo mundo, uma thread que entrasse em secao
   atomica mexeria no contador das outras, e alguem acabaria com o
   SIGALRM bloqueado para sempre - ou seja, sem preempcao nenhuma. */
static int *nivel_atomico_atual(void)
{
    return (atual >= 0) ? &threads[atual].nivel_atomico : &nivel_main;
}

void entra_atomico(void)
{
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGALRM);
    sigprocmask(SIG_BLOCK, &m, NULL);
    (*nivel_atomico_atual())++;
}

void sai_atomico(void)
{
    sigset_t m;
    int *nivel = nivel_atomico_atual();

    if (*nivel <= 0) return;        /* sai_atomico() sobrando        */
    (*nivel)--;
    if (*nivel > 0) return;         /* ainda tem secao por fora      */

    sigemptyset(&m);
    sigaddset(&m, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &m, NULL);
}

/* ===================================================================
   3) PREEMPCAO: temporizador e tratador do SIGALRM
   =================================================================== */

/* Chamado pelo kernel a cada QUANTUM_MS, DENTRO da thread que estava
   rodando. O swapcontext salva a thread exatamente onde ela estava
   (inclusive aqui dentro do tratador) e volta para o escalonador.

   Quando o escalonador escolher esta thread de novo, a execucao
   retorna para ca, o tratador termina normalmente, e o sistema
   operacional devolve a thread ao ponto em que ela foi interrompida.

   Obs: o tratador so roda dentro de uma thread. No escalonador o
   SIGALRM fica bloqueado, entao nao ha risco de o sinal cair no meio
   de uma troca de contexto.                                         */
static void trata_alarme(int sinal)
{
    (void)sinal;
    if (atual < 0) return;               /* seguranca                  */
    n_preempcoes++;
    swapcontext(&threads[atual].contexto, &ctx_escalonador);
}

static void instala_temporizador(void)
{
    struct sigaction sa;
    struct itimerval tempo;

    /* --- tratador do sinal --- */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = trata_alarme;
    sigemptyset(&sa.sa_mask);
    /* SA_RESTART: se o sinal interromper uma chamada de sistema das
       outras threads (fopen, read...), o sistema a repete sozinho,
       em vez de devolver erro EINTR.                                 */
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    /* --- temporizador que dispara de QUANTUM_MS em QUANTUM_MS --- */
    tempo.it_value.tv_sec     = 0;
    tempo.it_value.tv_usec    = QUANTUM_MS * 1000;   /* primeiro disparo */
    tempo.it_interval.tv_sec  = 0;
    tempo.it_interval.tv_usec = QUANTUM_MS * 1000;   /* e os seguintes   */
    setitimer(ITIMER_REAL, &tempo, NULL);
}

static void desliga_temporizador(void)
{
    struct itimerval zero;
    memset(&zero, 0, sizeof(zero));
    setitimer(ITIMER_REAL, &zero, NULL);
}


/* ===================================================================
   4) CRIACAO DE THREADS
   =================================================================== */
void escalonador_init(void)
{
    n_threads     = 0;
    atual         = -1;
    parar         = 0;
    n_trocas      = 0;
    n_preempcoes  = 0;
    nivel_main    = 0;
    teclado_init();
}

/* Funcao "embrulho". O makecontext aponta para ela, e nao direto para
   a funcao da thread, por dois motivos:
     - ela entrega o argumento guardado no TCB (makecontext so passa
       inteiros, entao um ponteiro nao caberia direito);
     - quando a funcao da thread retorna, ela encerra a thread.
   Assim quem escreve a thread so precisa dar 'return'.              */
static void trampolim(void)
{
    threads[atual].funcao(threads[atual].arg);
    thread_termina();
}

int thread_cria(void (*funcao)(void *), void *arg,
                const char *nome, int peso)
{
    tcb_t *t;

    if (n_threads >= MAX_THREADS) {
        fprintf(stderr, "escalonador: limite de %d threads atingido\n",
                MAX_THREADS);
        return -1;
    }
    if (funcao == NULL) {
        fprintf(stderr, "escalonador: funcao nula\n");
        return -1;
    }

    t = &threads[n_threads];
    t->id         = n_threads;
    t->estado     = PRONTA;
    t->peso       = (peso < 1) ? 1 : peso;
    t->creditos   = 0;
    t->acordar_em = 0;
    t->funcao     = funcao;
    t->arg           = arg;
    t->nivel_atomico = 0;
    strncpy(t->nome, nome, sizeof(t->nome) - 1);
    t->nome[sizeof(t->nome) - 1] = '\0';

    getcontext(&t->contexto);
    t->contexto.uc_stack.ss_sp    = t->pilha;
    t->contexto.uc_stack.ss_size  = TAM_PILHA;
    t->contexto.uc_stack.ss_flags = 0;
    t->contexto.uc_link           = &ctx_escalonador;

    /* A thread roda com o SIGALRM DESBLOQUEADO, senao nunca sofreria
       preempcao. O escalonador roda com ele bloqueado. Quem cuida da
       troca das mascaras e o proprio swapcontext, que salva e
       restaura a mascara junto com o resto do contexto.             */
    sigdelset(&t->contexto.uc_sigmask, SIGALRM);

    makecontext(&t->contexto, trampolim, 0);

    n_threads++;
    return t->id;
}


/* ===================================================================
   5) CHAMADAS FEITAS DE DENTRO DAS THREADS
   =================================================================== */

/* Devolve voluntariamente o resto do quantum.

   O bloqueio do SIGALRM em volta do swapcontext NAO e detalhe: sem
   ele, o sinal pode chegar no meio da propria troca de contexto, o
   tratador faz um segundo swapcontext por cima do primeiro e o estado
   da thread fica corrompido (o programa trava sem dar erro).

   Depois do swapcontext a mascara em vigor e a do escalonador, entao
   restauramos a que a thread tinha antes da chamada - o que tambem
   preserva uma eventual secao atomica em volta.                     */
void cede_processador(void)
{
    sigset_t antiga;
    if (atual < 0) return;

    bloqueia_alarme(&antiga);
    swapcontext(&threads[atual].contexto, &ctx_escalonador);
    restaura_mascara(&antiga);
}

/* Mesma protecao: marcar o estado e anotar a hora de acordar tem que
   acontecer sem o tratador do sinal entrar no meio.                 */
void thread_dorme(int ms)
{
    sigset_t antiga;

    if (atual < 0) { escalonador_cochila(ms); return; }
    if (ms < 0) ms = 0;

    bloqueia_alarme(&antiga);
    threads[atual].estado     = DORMINDO;
    threads[atual].acordar_em = tempo_agora_ms() + ms;
    swapcontext(&threads[atual].contexto, &ctx_escalonador);
    restaura_mascara(&antiga);
}

/* Encerra a thread. Usa setcontext e nao swapcontext porque nao ha
   motivo para salvar o estado de quem nunca mais vai rodar.
   Esta funcao nao retorna, entao a mascara nao precisa ser
   restaurada: quem assume e o contexto do escalonador.             */
void thread_termina(void)
{
    sigset_t m;
    if (atual < 0) return;

    sigemptyset(&m);
    sigaddset(&m, SIGALRM);
    sigprocmask(SIG_BLOCK, &m, NULL);

    threads[atual].estado = TERMINADA;
    setcontext(&ctx_escalonador);
}

int         thread_id_atual(void)   { return atual; }
const char *thread_nome_atual(void) { return (atual < 0) ? "escalonador"
                                                         : threads[atual].nome; }
long escalonador_trocas(void)       { return n_trocas; }
long escalonador_preempcoes(void)   { return n_preempcoes; }


/* ===================================================================
   6) PARADA DO PROGRAMA (tecla 's')
   =================================================================== */
void escalonador_pede_parada(void) { parar = 1; }
int  escalonador_deve_parar(void)  { return parar; }


/* ===================================================================
   7) POLITICA DE ESCALONAMENTO
   =================================================================== */

static void acorda_quem_pode(void)
{
    long agora = tempo_agora_ms();
    int  i;
    for (i = 0; i < n_threads; i++)
        if (threads[i].estado == DORMINDO && agora >= threads[i].acordar_em)
            threads[i].estado = PRONTA;
}

static int existe_thread_viva(void)
{
    int i;
    for (i = 0; i < n_threads; i++)
        if (threads[i].estado != TERMINADA)
            return 1;
    return 0;
}

/* POLITICA: round-robin circular com peso.
   Uma thread de peso P recebe P quanta seguidos antes de passar a vez.
   O Atendente nasce com peso maior que a Recepcao porque e ele quem
   determina a taxa de satisfacao (a nota e competitiva). Para ajustar
   o desempenho, mudar os pesos na criacao das threads.
   Retorna -1 se ninguem esta pronto.                                */
static int escolhe_proxima(void)
{
    int i, cand;

    /* ainda tem credito e continua pronta: segue com ela */
    if (atual >= 0 && threads[atual].estado == PRONTA
                   && threads[atual].creditos > 0) {
        threads[atual].creditos--;
        return atual;
    }

    /* senao, anda em circulo a partir da proxima. Comecar em i=1 e o
       que garante o rodizio: a thread atual so e reavaliada por
       ultimo, quando i == n_threads.                                */
    for (i = 1; i <= n_threads; i++) {
        cand = (atual + i) % n_threads;
        if (threads[cand].estado == PRONTA) {
            threads[cand].creditos = threads[cand].peso - 1;
            return cand;
        }
    }
    return -1;
}


/* ===================================================================
   8) LACO PRINCIPAL
   =================================================================== */
void escalonador_executa(void)
{
    int prox, tecla;

    /* O escalonador roda com o SIGALRM bloqueado do inicio ao fim.
       Como o swapcontext salva a mascara junto com o contexto, essa
       mascara volta sozinha toda vez que o controle retorna para ca. */
    sigset_t mascara_do_main;
    bloqueia_alarme(&mascara_do_main);
    instala_temporizador();

    while (1) {

        /* 1) esvazia o teclado (laco, e nao uma leitura so, para o
              's' nao ficar atras de outras teclas digitadas)        */
        while ((tecla = teclado_tecla()) != -1)
            if (tecla == 's' || tecla == 'S')
                parar = 1;

        /* 2) acorda quem ja dormiu o bastante                       */
        acorda_quem_pode();

        /* 3) acabou?                                                */
        if (!existe_thread_viva())
            break;

        /* 4) escolhe pela politica                                  */
        prox = escolhe_proxima();
        if (prox < 0) {
            escalonador_cochila(1);   /* todas dormindo: nao queima CPU */
            continue;
        }

        /* 5) TROCA DE CONTEXTO para dentro da thread. A thread roda
              ate: chamar cede_processador(), dormir, terminar, ou
              levar um SIGALRM (preempcao).                          */
        atual = prox;
        n_trocas++;
        swapcontext(&ctx_escalonador, &threads[atual].contexto);
    }

    atual = -1;
    desliga_temporizador();
    restaura_mascara(&mascara_do_main);
    teclado_restaura();
}
