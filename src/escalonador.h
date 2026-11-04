#ifndef ESCALONADOR_H
#define ESCALONADOR_H

#include <ucontext.h>

/* ===================================================================
   ESCALONADOR PREEMPTIVO DE THREADS DE USUARIO  -  Pessoa 1

   AS TRES PECAS

   1. ucontext: cada thread tem sua "foto" da CPU e sua pilha.
        getcontext()     tira a foto do estado atual
        makecontext()    prepara uma foto nova (funcao + pilha)
        swapcontext(a,b) salva o estado em 'a' e carrega 'b'
                         -> e literalmente a troca de contexto

   2. SIGALRM + setitimer: um temporizador dispara o sinal a cada
      QUANTUM_MS. O tratador do sinal faz swapcontext de volta para o
      escalonador. E assim que a thread perde a CPU SEM querer: e o
      equivalente a interrupcao de relogio de um SO de verdade.

   3. Mascara de sinal: enquanto o escalonador roda, o SIGALRM fica
      BLOQUEADO (senao o sinal cairia no meio da troca e corromperia
      tudo). Ao entrar numa thread, ele e desbloqueado.

   CONSEQUENCIA PARA AS OUTRAS PARTES
   Como a preempcao pode acontecer a qualquer instante, duas threads
   podem ser interrompidas no meio de uma operacao sobre a fila. Por
   isso a fila e protegida bloqueando o SIGALRM durante insercao e
   retirada - e exatamente o que o atomico.h da Pessoa 2 faz. Nao e
   mutex: e a mesma ideia de "desabilitar interrupcoes" que o SO usa.

   Mesmo com preempcao, vale chamar cede_processador() quando a thread
   nao tem o que fazer (fila vazia, esperando o cliente parar). Assim
   ela devolve o resto do quantum em vez de queimar CPU a toa.
   =================================================================== */

#define MAX_THREADS  4
#define TAM_PILHA    (64 * 1024)  /* 64 KB por thread                 */
#define QUANTUM_MS   10           /* fatia de tempo de cada thread    */

/* estados de uma thread */
#define PRONTA     0
#define DORMINDO   1
#define TERMINADA  2

/* TCB (Task Control Block): tudo o que o escalonador precisa saber
   sobre uma thread enquanto ela NAO esta rodando.                   */
typedef struct {
    int   id;
    int   estado;
    int   peso;               /* quantos quanta seguidos ela ganha    */
    int   creditos;           /* quanta que ainda restam neste turno  */
    long  acordar_em;         /* instante (ms) de voltar a ficar PRONTA */
    char  nome[16];
    void (*funcao)(void *);   /* corpo da thread                      */
    void *arg;                /* argumento passado na criacao         */
    int   nivel_atomico;      /* secoes atomicas aninhadas desta thread */
    ucontext_t contexto;
    char  pilha[TAM_PILHA];
} tcb_t;

/* relogio monotonico em ms (nunca anda para tras) */
long tempo_agora_ms(void);

/* ciclo de vida */
void escalonador_init(void);
int  thread_cria(void (*funcao)(void *), void *arg,
                 const char *nome, int peso);
void escalonador_executa(void);   /* so retorna quando todas acabarem */

/* chamadas usadas de dentro das threads */
void cede_processador(void);      /* devolve o resto do quantum       */
void thread_dorme(int ms);        /* dorme sem travar as outras       */
void thread_termina(void);        /* encerra a thread atual           */

/* consultas */
int         thread_id_atual(void);
const char *thread_nome_atual(void);
long        escalonador_trocas(void);     /* trocas de contexto       */
long        escalonador_preempcoes(void); /* quantas foram forcadas   */

/* encerramento do programa (tecla 's') */
void escalonador_pede_parada(void);
int  escalonador_deve_parar(void);

/* --- secao atomica -------------------------------------------------
   Bloqueia o SIGALRM, ou seja, impede a preempcao. Use em volta de
   qualquer trecho que nao pode ser interrompido pela metade.
   As chamadas podem ser aninhadas: cada entra_atomico() precisa de um
   sai_atomico() correspondente.
   Se a equipe ja tiver o atomico.h, use o dela: faz a mesma coisa. */
void entra_atomico(void);
void sai_atomico(void);

#endif
