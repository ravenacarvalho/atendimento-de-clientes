# atendimento-de-clientes

Trabalho de Sistemas Operacionais (UFC): simulação de um sistema de **atendimento de clientes** em C, para Linux, com **threads de usuário** implementadas com troca de contexto (`makecontext` / `swapcontext`), sem bibliotecas de paralelismo ou de comunicação entre processos.

## Enunciado resumido

O processo `atendimento` recebe `N` (número de clientes a gerar; `0` = infinitos) e `X` (paciência máxima, em ms, de um cliente de prioridade baixa).

- Cliente de **prioridade alta** tem paciência `X/2` ms; de **prioridade baixa**, `X` ms. A prioridade é sorteada (50% / 50%) na criação do cliente.
- Um cliente fica **satisfeito** se `(hora do fim do atendimento − hora de chegada) <= paciência`.
- A **Recepção** cria os clientes (processos) e os coloca numa fila de até 100 posições.
- O **Atendente** retira clientes da fila, grava o PID de cada um atendido no arquivo LNG e acorda o **Analista**, que imprime os PIDs na tela (no máximo 10 por vez) e os apaga do arquivo.
- Ao terminar, `atendimento` imprime a **taxa de satisfação** e o **tempo total de execução**. Com `N = 0`, o programa para quando a tecla `s` é apertada; a taxa considera só os clientes atendidos, e o arquivo LNG deve ser esvaziado antes de encerrar.

## Autores

| Parte | Responsável |
|---|---|
| Escalonador (troca de contexto, política, tecla `s`, encerramento) | [@RaissaSousaa](https://github.com/RaissaSousaa) |
| Recepção e fila | [@ravenacarvalho](https://github.com/ravenacarvalho) |
| Atendente e Analista | [@FelipeGabriel05](https://github.com/FelipeGabriel05) |

## Estado atual

| Módulo | Pasta | Situação |
|---|---|---|
| Escalonador (troca de contexto, política, tecla `s`, relatório final) | `src/escalonador/`, `src/input-output/` | Implementado |
| Recepção e fila | `src/atendimento/recepcao.*`, `fila.*` | Implementado e testado |
| Atendente, `main` do Atendimento e Analista | `src/atendimento/atendente.*`, `atendimento.c` | **Pendente** (arquivos ainda vazios; `analista.c` não está no repositório) |

Enquanto o `main` e o Atendente não existirem, `make` / `make all` falha no link com `undefined reference to main`. O que já pode ser compilado e testado é a Recepção e a fila (`make test`).

## Requisitos

Linux, `gcc` e `make`.

## Compilação e testes

```bash
make test      # compila o cliente e roda os testes da fila e da Recepção
make           # compila cliente e atendimento (só funciona quando o main existir)
make run       # ./atendimento 10 1000 (idem)
make clean
```

`make test` usa o escalonador real, mas não precisa do Atendente nem do `main`. Os testes verificam: ordem de retirada da fila, limite de 100 clientes, validação de `N` e `X`, execução com `N` finito (40 clientes), `N = 0` com a fila cheia (pausa em 100, retomada ao abrir vaga, parada), parada pela tecla `s`, e falha limpa quando `./cliente` não existe. Também confere que nenhum processo filho, `demanda.txt` ou zumbi sobra no fim.

## Estrutura

```
.
├── cliente.c                    # processo Cliente (fornecido pelo professor)
├── Makefile
├── src/
│   ├── escalonador/
│   │   └── escalonador.c/.h     # threads de usuário, política, seção atômica
│   ├── input-output/
│   │   ├── teclado.c/.h         # leitura da tecla 's' sem bloquear
│   │   └── encerramento.c/.h    # taxa de satisfação e tempo total
│   └── atendimento/
│       ├── recepcao.c/.h        # thread Recepção, leitura de N e X, semáforos
│       ├── fila.c/.h            # fila de clientes (até 100), política por prazo
│       ├── atendente.c/.h       # (pendente) thread Atendente
│       └── atendimento.c        # (pendente) main do processo Atendimento
└── teste/
    └── teste_recepcao.c         # testes da fila e da Recepção
```

## Escalonador

- Cada thread tem seu contexto (`ucontext_t`) e uma pilha de 64 KB. No máximo 4 threads (`MAX_THREADS`).
- **Preempção:** um `setitimer` dispara `SIGALRM` a cada 10 ms (`QUANTUM_MS`). O tratador faz `swapcontext` de volta ao escalonador. O `SIGALRM` fica bloqueado enquanto o escalonador roda, para o sinal não cair no meio de uma troca.
- **Política:** round-robin circular com peso. Uma thread de peso `P` recebe `P` quanta seguidos antes de passar a vez. Os pesos são definidos em `thread_cria()`.
- **Chamadas para as threads:** `cede_processador()`, `thread_dorme(ms)`, `thread_termina()` e `tempo_agora_ms()` (relógio monotônico em ms).
- **Seção atômica:** `entra_atomico()` / `sai_atomico()` bloqueiam o `SIGALRM` (podem ser aninhadas; o contador é por thread).
- **Tecla `s`:** o escalonador lê o teclado a cada volta do laço e liga o pedido de parada (`escalonador_deve_parar()`).
- **Encerramento:** `encerramento_relatorio()` imprime clientes atendidos, satisfeitos, taxa (dividida pelos **atendidos**, como pede o enunciado para o caso do `s`) e tempo total.

## Recepção e fila

### Funcionamento da Recepção

1. Cria os semáforos nomeados `/sem_atend` e `/sem_block`, ambos com valor inicial 1 (apagando antes qualquer resto de execução anterior).
2. Cria um cliente por vez com `fork` + `exec` de `./cliente`; a hora de chegada (`tempo_agora_ms()`) é registrada na criação.
3. Espera o cliente se suspender (`SIGSTOP`), lê e apaga `demanda.txt`.
4. Sorteia a prioridade (50% / 50%) e insere o cliente na fila.
5. Com `N = 0`, repete indefinidamente; com a fila em 100 clientes, pausa até abrir vaga.
6. Para ao criar `N` clientes, em `recepcao_para()`, ou quando `s` é apertado.

### Decisões de projeto

**Política da fila: menor prazo primeiro.** Cada cliente recebe um prazo = `hora de chegada + paciência`. A fila sempre entrega o cliente de menor prazo (*earliest deadline first*), com desempate pela ordem de chegada. Clientes de prioridade alta (prazo mais curto) passam à frente, o que favorece a taxa de satisfação, e clientes de prioridade baixa que já esperaram bastante também chegam ao menor prazo, então não há inanição.

**Sincronização da fila.** As threads são de usuário e rodam em uma única thread do sistema operacional; a preempção vem do `SIGALRM`. Por isso a fila é protegida bloqueando `SIGALRM` durante inserção e retirada (`entra_atomico()` / `sai_atomico()`), e não com mutex.

**Chamadas de biblioteca em threads preemptáveis.** Uma preempção no meio do `malloc`, `fork` ou `printf` deixaria um lock interno preso para as outras threads. Por isso o `fork` roda em seção atômica, `demanda.txt` é lido com `open`/`read` (sem `malloc`), os avisos de erro são impressos em seção atômica e o sorteio usa um gerador próprio em vez de `rand()`.

**Espera pelo cliente sem travar as outras threads.** A Recepção usa `waitpid(..., WUNTRACED | WNOHANG)` e chama `cede_processador()` entre as tentativas. Um `waitpid` bloqueante pararia o processo inteiro, incluindo todas as threads de usuário. Há um timeout de 5 s caso o cliente nunca se suspenda.

**Um cliente por vez.** Todos os clientes usam o mesmo `demanda.txt`, então a Recepção só cria o próximo depois de ler e apagar o arquivo do anterior.

**Fila cheia (N = 0).** A Recepção dorme 1 ms por vez enquanto a fila está com 100, em vez de girar em loop. A hora de chegada é registrada na criação, depois da pausa, então o cliente não é prejudicado pelo tempo em que a fila estava cheia.

### Interface

```c
// recepcao.h
int  recepcao_le_parametros(int argc, char *argv[], long *n, long *x);
void recepcao_inicia(RECEPCAO *r, long n, FILA *fila);
void recepcao_thread(void *arg);          // corpo da thread (arg = RECEPCAO *)
void recepcao_para(RECEPCAO *r);          // pede para parar de criar clientes
void recepcao_remove_semaforos(void);     // remove os semáforos do sistema
int  recepcao_descarta_fila(FILA *f);     // mata os clientes que ficaram na fila

// fila.h
void fila_inicia(FILA *f, long x_ms);
int  fila_insere(FILA *f, const CLIENTE *c);   // 0 ok, -1 cheia
int  fila_retira(FILA *f, CLIENTE *c);         // devolve o de menor prazo; -1 se vazia
int  fila_tamanho / fila_cheia / fila_vazia(const FILA *f);
int  cliente_satisfeito(const FILA *f, const CLIENTE *c, long fim_ms);
```

## Atendente e Analista (pendente)

Segundo o diagrama do enunciado, o Atendente deve, para cada cliente: ler o próximo da fila, acordá-lo, esperar os semáforos `/sem_atend` e `/sem_block`, escrever o PID no arquivo LNG, liberar `/sem_block`, calcular a satisfação e acordar o Analista. O Analista nasce dormindo e, ao ser acordado, bloqueia o LNG, imprime e apaga até 10 PIDs e volta a dormir.

### Integração com a Recepção e o escalonador

No `main`, depois de `escalonador_init()`:

```c
long n, x;  FILA fila;  RECEPCAO r;
if (recepcao_le_parametros(argc, argv, &n, &x) != 0) return 1;
fila_inicia(&fila, x);
recepcao_inicia(&r, n, &fila);
thread_cria(recepcao_thread, &r, "recepcao", 1);
/* ... thread do Atendente ... */
escalonador_executa();
recepcao_descarta_fila(&fila);   // só se sobrou gente (caso do 's')
recepcao_remove_semaforos();
```

Pontos que o Atendente precisa respeitar:

- **Fim do atendimento:** usar `tempo_agora_ms()` como `fim_ms` em `cliente_satisfeito()`. A chegada foi gravada com a mesma função, então a diferença `fim − chegada` é correta.
- **Término:** com `N` finito, o Atendente acaba quando `r.finalizada` está ligado, a fila está vazia e ele atendeu `r.criados` clientes. Cheque também `r.erro`.
- **Nunca bloquear de verdade:** `sem_wait` e `waitpid` bloqueantes param o processo inteiro (todas as threads). Usar `sem_trywait` / `waitpid(..., WNOHANG)` com `cede_processador()` entre as tentativas.
- **Semáforos:** já estão abertos em `r.sem_atend` e `r.sem_block`; `r.semaforos_prontos` indica quando existem.
- **Recolher o cliente:** depois que o cliente termina, é preciso um `waitpid` nele, senão vira zumbi.
- **`fila_retira` libera a vaga no início do atendimento**, então a Recepção já pode criar o próximo cliente enquanto este é atendido.
- **Tecla `s` (N = 0):** esvaziar o LNG antes de encerrar, acordando o Analista quantas vezes forem necessárias, e chamar `encerramento_relatorio()` com os clientes atendidos e satisfeitos.