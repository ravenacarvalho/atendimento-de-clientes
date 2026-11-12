#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "fila.h"
#include "recepcao.h"
#include "escalonador.h"

// testes da fila e da recepcao, rodando em cima do escalonador de verdade
// precisa do ./cliente na pasta (o make test ja compila antes)

/* RESUMO: o teste tem 6 grupos:
   1) fila            
   2) parametros N e X
   3) N finito (40 clientes)
   4) N=0 + fila cheia
   5) tecla 's'
   6) ./cliente ausente
*/

static int total = 0, falhas = 0;

static void confere(int ok, const char *desc)
{
    entra_atomico();
    total++;
    if (!ok) falhas++;
    printf("  [%s] %s\n", ok ? "ok" : "FALHOU", desc);
    fflush(stdout);
    sai_atomico();
}

static void titulo(const char *t) { printf("\n%s\n", t); fflush(stdout); }

// sobrou algum filho vivo, suspenso ou zumbi?
static int sem_filhos(void)
{
    int st;
    return waitpid(-1, &st, WNOHANG | WUNTRACED) == -1 && errno == ECHILD;
}

// fila
static CLIENTE mk(pid_t pid, int prio, long chegada)
{
    CLIENTE c;
    memset(&c, 0, sizeof(c));
    c.pid = pid; c.prioridade = prio; c.chegada_ms = chegada;
    return c;
}

// GRUPO 1 - FILA: testa so a fila, sem processos.
// confere (a) a ordem de saida: menor prazo primeiro, alta prioridade passa na
// frente e empate desempata por quem chegou antes; (b) o limite de 100 clientes;
// (c) a regra de satisfacao (tempo de espera <= paciencia)
static void teste_fila(void)
{
    FILA f;
    CLIENTE c, saida;
    int i, ok;

    titulo("Fila: ordem de retirada (menor prazo primeiro)");
    fila_inicia(&f, 1000);                 /* alta = 500, baixa = 1000 */

    c = mk(1, PRIORIDADE_BAIXA, 0);   fila_insere(&f, &c); /* prazo 1000 */
    c = mk(2, PRIORIDADE_ALTA,  100); fila_insere(&f, &c); /* prazo  600 */
    c = mk(3, PRIORIDADE_ALTA,  50);  fila_insere(&f, &c); /* prazo  550 */
    c = mk(4, PRIORIDADE_BAIXA, 10);  fila_insere(&f, &c); /* prazo 1010 */
    c = mk(5, PRIORIDADE_ALTA,  500); fila_insere(&f, &c); /* prazo 1000 */

    ok = 1;
    { pid_t esperado[5] = {3, 2, 1, 5, 4};  /* 1 e 5 empatam: 1 chegou antes */
      for (i = 0; i < 5; i++)
          if (fila_retira(&f, &saida) != 0 || saida.pid != esperado[i]) ok = 0; }
    confere(ok, "ordem 3,2,1,5,4 (alta passa a frente; empate por chegada)");
    confere(fila_vazia(&f) && fila_retira(&f, &saida) == -1,
            "fila vazia devolve -1");

    titulo("Fila: limite de 100 clientes");
    fila_inicia(&f, 1000);
    ok = 1;
    for (i = 0; i < FILA_CAPACIDADE; i++) {
        c = mk(100 + i, PRIORIDADE_BAIXA, i);
        if (fila_insere(&f, &c) != 0) ok = 0;
    }
    confere(ok && fila_cheia(&f) && fila_tamanho(&f) == 100,
            "aceita 100 clientes");
    c = mk(999, PRIORIDADE_ALTA, 0);
    confere(fila_insere(&f, &c) == -1 && fila_tamanho(&f) == 100,
            "o 101o e recusado e a fila nao muda");
    fila_retira(&f, &saida);
    confere(!fila_cheia(&f) && fila_insere(&f, &c) == 0,
            "abre vaga ao retirar e volta a aceitar");

    titulo("Fila: satisfacao");
    fila_inicia(&f, 1000);
    c = mk(1, PRIORIDADE_ALTA, 100);
    confere(cliente_satisfeito(&f, &c, 600) == 1, "alta, 500 ms espera: satisfeito (<=)");
    confere(cliente_satisfeito(&f, &c, 601) == 0, "alta, 501 ms de espera: insatisfeito");
    c = mk(2, PRIORIDADE_BAIXA, 100);
    confere(cliente_satisfeito(&f, &c, 1100) == 1, "baixa, 1000 ms: satisfeito");
    confere(cliente_satisfeito(&f, &c, 1101) == 0, "baixa, 1001 ms: insatisfeito");
}

// GRUPO 2 - PARAMETROS: testa a leitura de "./atendimento N X".
// aceita valores validos (inclusive N=0) e recusa: falta ou sobra de argumento,
// texto que nao e numero ("abc", "10x", vazio), N negativo, X zero ou negativo
static void teste_parametros(void)
{
    long n = -7, x = -7;
    char *ok1[]   = {"atend", "10", "1000"};
    char *ok2[]   = {"atend", "0", "1"};
    char *falta[] = {"atend", "10"};
    char *sobra[] = {"atend", "10", "100", "5"};
    char *letra[] = {"atend", "10", "abc"};
    char *mista[] = {"atend", "10x", "100"};
    char *negN[]  = {"atend", "-1", "100"};
    char *zeroX[] = {"atend", "5", "0"};
    char *negX[]  = {"atend", "5", "-3"};
    char *vazio[] = {"atend", "", "100"};

    titulo("Parametros N e X (as mensagens de uso abaixo sao esperadas)");
    confere(recepcao_le_parametros(3, ok1, &n, &x) == 0 && n == 10 && x == 1000,
            "10 1000 aceito");
    confere(recepcao_le_parametros(3, ok2, &n, &x) == 0 && n == 0 && x == 1,
            "N=0 (infinito) e X=1 aceitos");
    confere(recepcao_le_parametros(2, falta, &n, &x) != 0, "falta argumento recusado");
    confere(recepcao_le_parametros(4, sobra, &n, &x) != 0, "argumento sobrando recusado");
    confere(recepcao_le_parametros(3, letra, &n, &x) != 0, "X nao numerico recusado");
    confere(recepcao_le_parametros(3, mista, &n, &x) != 0, "'10x' recusado");
    confere(recepcao_le_parametros(3, negN, &n, &x) != 0, "N negativo recusado");
    confere(recepcao_le_parametros(3, zeroX, &n, &x) != 0, "X = 0 recusado");
    confere(recepcao_le_parametros(3, negX, &n, &x) != 0, "X negativo recusado");
    confere(recepcao_le_parametros(3, vazio, &n, &x) != 0, "N vazio recusado");
}

// CONSUMIDOR (Atendente falso): faz o papel do atendente (acorda o cliente e espera ele terminar)
// so pra exercitar a recepcao, enquanto o atendente de verdade nao existe.
// retira da fila, manda SIGCONT no cliente, espera ele acabar e confere os dados
typedef struct {
    RECEPCAO *r;
    FILA     *fila;
    long atendidos, satisfeitos, altas, baixas;
    int  tempos_validos;       /* 1 enquanto todo tempo lido foi 1, 5 ou 15 */
    int  prazos_corretos;
} CONSUMIDOR;

static void consumidor(void *arg)
{
    CONSUMIDOR *k = arg;
    CLIENTE c;
    int st;

    k->tempos_validos = k->prazos_corretos = 1;
    for (;;) {
        if (fila_retira(k->fila, &c) == 0) {
            long ate = tempo_agora_ms() + 5000;
            if (c.tempo_atendimento != 1 && c.tempo_atendimento != 5 &&
                c.tempo_atendimento != 15)
                k->tempos_validos = 0;
            if (c.prazo_ms != c.chegada_ms + fila_paciencia_ms(k->fila, c.prioridade))
                k->prazos_corretos = 0;
            if (c.prioridade == PRIORIDADE_ALTA) k->altas++; else k->baixas++;

            kill(c.pid, SIGCONT);
            while (waitpid(c.pid, &st, WNOHANG) != c.pid &&
                   tempo_agora_ms() < ate)
                cede_processador();
            k->atendidos++;
            if (cliente_satisfeito(k->fila, &c, tempo_agora_ms()))
                k->satisfeitos++;
        } else if (k->r->finalizada) {
            break;
        } else {
            cede_processador();
        }
    }
}

// GRUPO 3 - N FINITO: Recepcao + consumidor com N = 40 clientes.
// confere que criou exatamente 40, todos foram atendidos, a fila esvaziou,
// o tempo lido e sempre 1, 5 ou 15, o prazo esta certo (chegada + paciencia),
// as duas prioridades apareceram, demanda.txt foi apagado e nao sobrou processo filho
static void teste_n_finito(void)
{
    enum { N = 40 };
    FILA f; RECEPCAO r; CONSUMIDOR k;
    long t0;

    titulo("Recepcao com N = 40 finito");
    memset(&k, 0, sizeof(k));
    fila_inicia(&f, 60000);
    recepcao_inicia(&r, N, &f);
    k.r = &r; k.fila = &f;

    escalonador_init();
    thread_cria(recepcao_thread, &r, "recepcao", 1);
    thread_cria(consumidor, &k, "consumidor", 1);
    t0 = tempo_agora_ms();
    escalonador_executa();
    t0 = tempo_agora_ms() - t0;

    confere(r.semaforos_prontos, "semaforos criados");
    confere(!r.erro && r.finalizada, "terminou sem erro");
    confere(r.criados == N, "criou exatamente N clientes");
    confere(k.atendidos == N, "todos os N chegaram ao consumidor");
    confere(fila_vazia(&f), "fila vazia no fim");
    confere(k.tempos_validos, "demanda lida e sempre 1, 5 ou 15");
    confere(k.prazos_corretos, "prazo = chegada + paciencia");
    confere(k.altas > 0 && k.baixas > 0, "as duas prioridades foram sorteadas");
    printf("      (altas=%ld baixas=%ld, %ld ms no total, %.1f ms/cliente)\n",
           k.altas, k.baixas, t0, (double)t0 / N);
    confere(k.satisfeitos == N, "com X grande todos ficaram satisfeitos");
    confere(access(DEMANDA_ARQ, F_OK) != 0, "demanda.txt apagado");
    confere(sem_filhos(), "nenhum processo filho sobrando");
    recepcao_remove_semaforos();
}

// GRUPO 4 - N = 0 COM FILA CHEIA: sem consumidor, a fila tem que encher.
// a thread "controle" confere 3 coisas: (1) a Recepcao PAUSA em exatamente 100;
// (2) ao abrir 5 vagas, ela RETOMA, cria 5 e para de novo em 100;
// (3) recepcao_para() encerra a Recepcao sem erro
typedef struct {
    RECEPCAO *r;
    FILA     *fila;
    int       pausou, retomou, parou_ok;
    long      criados_na_pausa;
} CONTROLE;

static int espera_ate(int (*cond)(CONTROLE *), CONTROLE *c, long ms)
{
    long ate = tempo_agora_ms() + ms;
    while (!cond(c)) {
        if (tempo_agora_ms() > ate) return 0;
        thread_dorme(5);
    }
    return 1;
}
static int cond_cheia(CONTROLE *c)  { return fila_cheia(c->fila); }
static int cond_final(CONTROLE *c)  { return c->r->finalizada; }

static void controle_fila_cheia(void *arg)
{
    CONTROLE *c = arg;
    CLIENTE x;
    int i;

    /* 1) sem consumidor, a fila enche e a criacao para em 100 */
    if (!espera_ate(cond_cheia, c, 30000)) return;
    thread_dorme(400);
    c->criados_na_pausa = c->r->criados;
    c->pausou = (c->r->criados == 100 && fila_tamanho(c->fila) == 100);

    /* 2) libera 5 vagas: a Recepcao deve voltar a criar ate 100 de novo */
    for (i = 0; i < 5; i++)
        if (fila_retira(c->fila, &x) == 0) {
            kill(x.pid, SIGKILL);
            waitpid(x.pid, NULL, 0);
        }
    {
        long ate = tempo_agora_ms() + 10000;
        while (!(c->r->criados >= 105 && fila_cheia(c->fila)) &&
               tempo_agora_ms() < ate)
            thread_dorme(5);
    }
    thread_dorme(300);
    c->retomou = (c->r->criados == 105 && fila_tamanho(c->fila) == 100);

    /* 3) para */
    recepcao_para(c->r);
    c->parou_ok = espera_ate(cond_final, c, 5000);
}

static void teste_n_zero_fila_cheia(void)
{
    FILA f; RECEPCAO r; CONTROLE c;
    int descartados;

    titulo("Recepcao com N = 0 e fila cheia (leva alguns segundos)");
    memset(&c, 0, sizeof(c));
    fila_inicia(&f, 1000);
    recepcao_inicia(&r, 0, &f);
    c.r = &r; c.fila = &f;

    escalonador_init();
    thread_cria(recepcao_thread, &r, "recepcao", 1);
    thread_cria(controle_fila_cheia, &c, "controle", 1);
    escalonador_executa();

    confere(c.pausou, "parou de criar com 100 na fila (criou exatamente 100)");
    confere(c.retomou, "ao abrir 5 vagas, criou mais 5 e parou de novo em 100");
    confere(c.parou_ok && r.finalizada && !r.erro,
            "recepcao_para() encerrou a Recepcao sem erro");
    confere(fila_tamanho(&f) == 100, "fila terminou com 100 clientes");
    descartados = recepcao_descarta_fila(&f);
    confere(descartados == 100 && fila_vazia(&f), "descartou os 100 da fila");
    confere(sem_filhos(), "nenhum processo filho sobrando");
    recepcao_remove_semaforos();
}

// GRUPO 5 - TECLA 's': N = 0 (infinito) parado pelo 's'.
// (chamo escalonador_pede_parada, que e o que a tecla faz; a leitura real do
// teclado e da Pessoa 1). confere que a Recepcao para sozinha e que tudo que
// foi criado foi atendido
static void controle_tecla_s(void *arg)
{
    RECEPCAO *r = arg;
    long ate = tempo_agora_ms() + 10000;
    while (r->criados < 5 && tempo_agora_ms() < ate)
        thread_dorme(5);
    escalonador_pede_parada();        /* o mesmo que o 's' faz */
}

static void teste_tecla_s(void)
{
    FILA f; RECEPCAO r; CONSUMIDOR k;

    titulo("Recepcao com N = 0 parada pela tecla 's'");
    memset(&k, 0, sizeof(k));
    fila_inicia(&f, 1000);
    recepcao_inicia(&r, 0, &f);
    k.r = &r; k.fila = &f;

    escalonador_init();
    thread_cria(recepcao_thread, &r, "recepcao", 1);
    thread_cria(consumidor, &k, "consumidor", 1);
    thread_cria(controle_tecla_s, &r, "tecla_s", 1);
    escalonador_executa();

    confere(r.finalizada && !r.erro, "Recepcao terminou sozinha ao ver o 's'");
    confere(r.criados >= 5, "tinha criado clientes ate ali");
    confere(k.atendidos == r.criados && fila_vazia(&f),
            "tudo que foi criado foi atendido (nada ficou na fila)");
    confere(sem_filhos(), "nenhum processo filho sobrando");
    recepcao_remove_semaforos();
}

// GRUPO 6 - SEM ./cliente: sem o ./cliente na pasta tem que falhar limpo, sem travar.
// o teste muda para /tmp (onde nao ha ./cliente) e confere erro=1, nenhum cliente
// criado e nenhum processo sobrando. (e o timeout/erro de exec funcionando)
static void teste_sem_cliente(void)
{
    FILA f; RECEPCAO r;
    char antigo[512];

    titulo("Executavel ./cliente ausente (mensagem de erro esperada)");
    if (getcwd(antigo, sizeof(antigo)) == NULL || chdir("/tmp") != 0) {
        confere(0, "nao consegui mudar de diretorio"); return;
    }
    fila_inicia(&f, 1000);
    recepcao_inicia(&r, 3, &f);
    escalonador_init();
    thread_cria(recepcao_thread, &r, "recepcao", 1);
    escalonador_executa();
    if (chdir(antigo) != 0) perror("chdir");

    confere(r.erro && r.finalizada && r.criados == 0,
            "falha limpa: erro=1, nada criado, sem travar");
    confere(sem_filhos(), "nenhum processo filho sobrando");
    recepcao_remove_semaforos();
}

int main(void)
{
    teste_fila();
    teste_parametros();
    teste_n_finito();
    teste_n_zero_fila_cheia();
    teste_tecla_s();
    teste_sem_cliente();

    printf("\n%d verificacoes, %d falha(s)\n", total, falhas);
    return falhas ? 1 : 0;
}