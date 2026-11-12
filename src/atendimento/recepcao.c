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
#include "escalonador.h" 

/*
   A Recepção é uma thread que cria processos Cliente, um de cada vez.
   Pra cada um: cria, espera ele se suspender (SIGSTOP = sinal de que o
   demanda.txt já foi escrito), lê o tempo de atendimento, sorteia a
   prioridade (50% cada) e põe na fila.
   Se a fila chega a 100, ela pausa até abrir vaga.
   No início, também cria os semáforos /sem_atend e /sem_block.
 */

// ---- funções de apoio ----

//escreve uma mensagem de erro na tela
static void avisa(const char *fmt, ...)
{
    va_list ap;

    entra_atomico();
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    sai_atomico();
}

// É usada para sortear a prioridade do cliente (1 = alta, 0 = baixa)
static unsigned prox_bit(void)
{
    unsigned bit;

    entra_atomico();
    bit = rand() % 2; // sorteia um num e divide por 2, o resultado é 0 ou 1
    sai_atomico();
    return bit;
}

// Diz se a Recepção deve parar de criar clientes. Devolve 1 (sim) ou 0 (não)
// Para se recepcao_para() foi chamada, OU se N = 0 (infinitos) e apertaram 's'
static int deve_parar(const RECEPCAO *r)
{
    return r->parar                          // pediram para parar via recepcao_para()
        || (r->n == 0                        // OU clientes infinitos (N = 0)...
            && escalonador_deve_parar());    //     e o 's' foi apertado
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

// Lê quantos clientes vão chegar (N) e quanto tempo cada um aguenta esperar (X)
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

/* apaga os dois semáforos do sistema. semáforo nomeado continua existindo
   depois que o programa acaba, então precisa ser removido no fim */
void recepcao_remove_semaforos(void)
{
    sem_unlink(SEM_ATEND_NOME);
    sem_unlink(SEM_BLOCK_NOME);
}

/* cria os dois semáforos nomeados. um semáforo é como uma chave compartilhada:
   quem pega a chave (sem_wait) faz sua tarefa e os outros esperam até ela
   ser devolvida (sem_post).
     /sem_atend: o Cliente usa durante o atendimento
     /sem_block: protege o arquivo dos PIDs (Atendente e Analista)
   devolve 0 (criou os dois) ou -1 (erro) */
static int cria_semaforos(RECEPCAO *r)
{
    // /sem_atend: o Cliente abre pelo nome, então precisa existir antes de qualquer cliente
    // O_CREAT = se não existir, cria (se já existir, só abre)
    // 0644    = permissões, como as de arquivo (dono lê e escreve, outros só leem)
    // 1       = valor inicial: a chave começa disponível
    r->sem_atend = sem_open(SEM_ATEND_NOME, O_CREAT, 0644, 1);

    if (r->sem_atend == SEM_FAILED) {   // não conseguiu criar
        avisa("recepcao: sem_open(%s): %s\n",
              SEM_ATEND_NOME, strerror(errno));   // strerror = errno em texto legível
        return -1;
    }

    // /sem_block: criado do mesmo jeito
    r->sem_block = sem_open(SEM_BLOCK_NOME, O_CREAT, 0644, 1);

    if (r->sem_block == SEM_FAILED) {   // não conseguiu criar
        avisa("recepcao: sem_open(%s): %s\n",
              SEM_BLOCK_NOME, strerror(errno));

        sem_close(r->sem_atend);   // o primeiro já estava aberto: fecha pra não deixar recurso aberto
        return -1;
    }

    return 0;   // os dois foram criados
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

// espera o cliente escrever o demanda.txt e se suspender (SIGSTOP)
/* uso WNOHANG (não bloqueia): as threads são de usuário, então se não parou
 cede a CPU e tento de novo
 devolve: 0 = parou | 1 = mandaram parar | -1 = erro */
static int espera_cliente_suspender(const RECEPCAO *r, pid_t pid)
{
    int status;
    long limite = tempo_agora_ms() + CLIENTE_TIMEOUT_MS;   // prazo máximo: 5 s

    for (;;) {   // repete até ter uma resposta
        // pergunta ao sistema o que houve com o cliente, SEM bloquear
        pid_t w = waitpid(pid, &status, WUNTRACED | WNOHANG);

        // 1) o cliente mudou de estado
        if (w == pid) {
            if (WIFSTOPPED(status)) return 0;   // parou: é o que eu esperava
            avisa("recepcao: o cliente terminou sem se suspender "
                  "(exec de %s falhou?)\n", CLIENTE_EXEC);
            return -1;                          // morreu sem parar: erro
        }
        // 2) erro do waitpid (EINTR = interrompido pelo SIGALRM, é normal, ignora)
        if (w < 0 && errno != EINTR) {
            avisa("recepcao: waitpid: %s\n", strerror(errno));
            return -1;
        }
        // 3) apertaram 's' (ou recepcao_para): mata o cliente e sai
        if (deve_parar(r)) {
            mata_cliente(pid);
            return 1;
        }
        // 4) passou de 5 s: algo deu errado, desiste pra não esperar pra sempre
        if (tempo_agora_ms() > limite) {
            avisa("recepcao: cliente nao se suspendeu em %d ms\n",
                  CLIENTE_TIMEOUT_MS);
            mata_cliente(pid);
            return -1;
        }
        // 5) ainda não parou: devolve a CPU pras outras threads e pergunta de novo
        cede_processador();
    }
}

// le o numero que o cliente escreveu e apaga o arquivo
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

/* Cria um cliente e põe na fila. devolve 0 (inseriu), 1 (mandaram parar) ou -1 (erro)
   fluxo: cria processo → anota chegada → espera parar → lê demanda
        → sorteia prioridade → insere na fila → conta
se uma etapa falha, mata o cliente (se ele já existe) e devolve o erro */ 
static int cria_cliente(RECEPCAO *r)
{
    CLIENTE c; // a "ficha" do cliente, que vai pra fila
    long tempo; // o tempo de atendimento que o cliente escreveu no demanda.txt
    int  esp;  // o resultado da espera pelo cliente (0, 1 ou -1)
    int  ok;       // resultado da leitura da demanda (0 = ok, -1 = falhou)
    pid_t pid; // o número (PID) do processo cliente

    // cria o processo 
    pid = cria_processo_cliente();
    if (pid < 0) return -1;

    // monta a ficha: chegada = momento da criação do cliente
    memset(&c, 0, sizeof(c));
    c.pid        = pid;
    c.chegada_ms = tempo_agora_ms();

    esp = espera_cliente_suspender(r, pid); // espera o cliente escrever a demanda e parar (SIGSTOP)
    if (esp != 0) return esp; //  se não deu certo para e devolve o código (1 = parar, -1 = erro)

    ok = le_demanda(&tempo);   // lê o tempo de atendimento em demanda.txt (e apaga o arquivo)
    if (ok != 0) {             // não deu certo
        avisa("recepcao: demanda invalida do cliente %d\n", (int)pid);
        mata_cliente(pid);     // ele está suspenso e nunca acordaria sozinho
        return -1;
    }
    c.tempo_atendimento = tempo; // guarda o tempo na ficha
    // sorteia a prioridade: alta (paciência X/2) ou baixa (paciência X)
    c.prioridade = prox_bit() ? PRIORIDADE_ALTA : PRIORIDADE_BAIXA;   

    // coloca na fila (não deveria falhar: a thread já confere se está cheia)
    if (fila_insere(r->fila, &c) != 0) {   
        avisa("recepcao: fila cheia na insercao\n");
        mata_cliente(pid); // não entrou na fila, então não pode ficar suspenso
        return -1;
    }
    r->criados++; // mais um cliente na fila
    return 0; // sucesso
}

// ---- a thread ----

void recepcao_inicia(RECEPCAO *r, long n, FILA *fila)
{
    memset(r, 0, sizeof(*r));
    r->n    = n;
    r->fila = fila;

    // semente do rand: mistura hora e pid pra nao dar a mesma sequencia toda vez
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
}

void recepcao_para(RECEPCAO *r)
{
    r->parar = 1;
}

// Decide quando criar o próximo cliente e quando parar
void recepcao_thread(void *arg)
{
    // o escalonador entrega um void* genérico. Converto de volta pra struct da Recepção
    RECEPCAO *r = (RECEPCAO *)arg; 
    if (cria_semaforos(r) != 0) { // cria_semaforos devolve 0 se deu certo e -1 se falhou
        r->erro = 1; // houve erro
        r->finalizada = 1; // a Recepção terminou, o Atendente não fica esperando clientes que não virão
        return;
    }
    r->semaforos_prontos = 1; // Avisa pro Atendente: os semáforos já existem, pode usar

    // Roda até criar N clientes. com N = 0 nunca acaba sozinho (só por parada ou erro)
    while (r->n == 0 || r->criados < r->n) {
        int res;

        // Se apertaram s (com N = 0) ou chamaram recepcao_para(), sai do laço na hora
        if (deve_parar(r)) break; 

        // fila com 100 clientes: não pode criar mais
        // espera abrir vaga, dormindo 1 ms entre as conferências
        if (fila_cheia(r->fila)) {
            thread_dorme(1);   // dorme 1 ms sem travar as outras threads (o Atendente pode retirar alguém)
            continue;          // volta ao início do laço e confere de novo (parar? fila cheia?)
        }

        res = cria_cliente(r); // Cria um cliente e guarda o resultado em res (0 = ok, 1 = parar, -1 = erro)
        if (res < 0) { r->erro = 1; break; } // erro: marca e sai
        if (res > 0) break;   // mandaram parar: sai 
    }
    r->finalizada = 1; // avisa o Atendente que não vêm mais clientes
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