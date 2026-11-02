# atendimento-de-clientes

Trabalho 2 de Sistemas Operacionais (UFC): simulação de um sistema de **atendimento de clientes** em C, para Linux, com **threads de usuário** implementadas com troca de contexto (`makecontext` / `swapcontext`), sem bibliotecas de paralelismo ou de comunicação entre processos.

## Enunciado resumido

O processo `atendimento` recebe `N` (número de clientes a gerar; `0` = infinitos) e `X` (paciência máxima, em ms, de um cliente de prioridade baixa).

- Cliente de **prioridade alta** tem paciência `X/2` ms; de **prioridade baixa**, `X` ms. A prioridade é sorteada (50% / 50%) na criação do cliente.
- Um cliente fica **satisfeito** se `(hora do fim do atendimento − hora de chegada) <= paciência`.
- A **Recepção** cria os clientes (processos) e os coloca numa fila de até 100 posições.

## Recepção e fila

### Requisitos

Linux, `gcc` e `make`.

### Compilação e testes

```bash
make            # compila o cliente
make test       # compila e roda os testes da fila e da recepção
make clean
```

Os testes verificam: ordem de retirada da fila, limite de 100 clientes, validação de `N` e `X`, execução com `N` finito, e `N = 0` com a fila cheia (pausa, retomada e parada).

### Estrutura

```
.
├── cliente.c            # processo Cliente (fornecido pelo professor)
├── Makefile
├── src/
│   ├── atomico.h        # seção atômica (bloqueio de SIGALRM)
│   ├── relogio.c/.h     # tempo em ms desde o início do programa
│   ├── fila.c/.h        # fila de clientes (até 100), política por prazo
│   └── recepcao.c/.h    # thread Recepção, leitura de N e X, semáforos
└── teste/
    └── teste_recepcao.c # testes da fila e da recepção (escalonador provisório)
```

### Funcionamento da Recepção

1. Cria os semáforos nomeados `/sem_atend` e `/sem_block`, ambos com valor inicial 1.
2. Cria um cliente por vez com `fork` + `exec` de `./cliente`.
3. Espera o cliente se suspender (`SIGSTOP`), lê e apaga `demanda.txt`.
4. Sorteia a prioridade (50% / 50%) e insere o cliente na fila.
5. Com `N = 0`, repete indefinidamente; com a fila em 100 clientes, pausa até abrir vaga.

### Decisões de projeto

**Política da fila: menor prazo primeiro.** Cada cliente recebe um prazo = `hora de chegada + paciência`. A fila sempre entrega o cliente de menor prazo (*earliest deadline first*), com desempate pela ordem de chegada. Clientes de prioridade alta (prazo mais curto) passam à frente, o que favorece a taxa de satisfação, e clientes de prioridade baixa que já esperaram bastante também chegam ao menor prazo, então não há inanição.

**Sincronização da fila.** As threads são de usuário e rodam em uma única thread do sistema operacional; a preempção vem do `SIGALRM`. Por isso a fila é protegida bloqueando `SIGALRM` durante inserção e retirada (`atomico.h`), e não com mutex. A máscara anterior é restaurada, permitindo seções aninhadas.

**Espera pelo cliente sem travar as outras threads.** A Recepção usa `waitpid(..., WUNTRACED | WNOHANG)` e chama `cede_processador()` entre as tentativas. Um `waitpid` bloqueante pararia o processo inteiro, incluindo todas as threads de usuário.

**Um cliente por vez.** Todos os clientes usam o mesmo `demanda.txt`, então a Recepção só cria o próximo depois de ler e apagar o arquivo do anterior.

**Fila cheia (N = 0).** A hora de chegada é registrada depois da pausa por fila cheia, para o cliente não ser prejudicado pelo tempo em que a fila estava cheia.

### Interface

```c
// recepcao.h
int  recepcao_le_parametros(int argc, char *argv[], long *n, long *x);
void recepcao_inicia(RECEPCAO *r, long n, FILA *fila);
void recepcao_thread(void *arg);          // corpo da thread (arg = RECEPCAO *)
void recepcao_para(RECEPCAO *r);          // pede para parar de criar clientes
void recepcao_remove_semaforos(void);     // remove os semáforos do sistema
void recepcao_descarta_fila(FILA *f);     // mata os clientes que ficaram na fila
extern void cede_processador(void);       // depende do escalonador

// fila.h
int  fila_insere(FILA *f, const CLIENTE *c);
int  fila_retira(FILA *f, CLIENTE *c);    // devolve o de menor prazo
int  cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms);
```

`relogio_inicia()` deve ser chamada no início do `main`, e `r->semaforos_prontos` indica quando os semáforos já existem. Os campos `r->finalizada` e `r->erro` indicam quando a Recepção terminou de criar clientes.