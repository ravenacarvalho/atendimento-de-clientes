
CC = gcc

CFLAGS = -Wall -Wextra -g \
         -Isrc \
         -Isrc/atendimento \
         -Isrc/escalonador \
         -Isrc/input-output \
         -Isrc/utils

LDFLAGS = -lrt

EXEC_ATENDIMENTO = atendimento
EXEC_ANALISTA = analista
EXEC_CLIENTE = cliente

# Fontes do processo Atendimento e suas threads
SRC_ATENDIMENTO = $(wildcard \
    src/atendimento/*.c \
    src/escalonador/*.c \
    src/input-output/*.c)

OBJ_ATENDIMENTO = $(SRC_ATENDIMENTO:.c=.o)

# Processo Analista
SRC_ANALISTA = src/processos/analista.c
OBJ_ANALISTA = $(SRC_ANALISTA:.c=.o)

# Processo Cliente fornecido
SRC_CLIENTE = src/processos/cliente.c
OBJ_CLIENTE = $(SRC_CLIENTE:.c=.o)


# Compila todos os executaveis
all: $(EXEC_CLIENTE) $(EXEC_ANALISTA) $(EXEC_ATENDIMENTO)


# Processo principal
$(EXEC_ATENDIMENTO): $(OBJ_ATENDIMENTO)
	$(CC) $(OBJ_ATENDIMENTO) -o $@ $(LDFLAGS)


# Processo Analista
$(EXEC_ANALISTA): $(OBJ_ANALISTA)
	$(CC) $(OBJ_ANALISTA) -o $@ $(LDFLAGS)


# Processo Cliente
$(EXEC_CLIENTE): $(OBJ_CLIENTE)
	$(CC) $(OBJ_CLIENTE) -o $@ $(LDFLAGS)


# Compilacao dos arquivos objeto
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


# Testes da Recepcao
EXEC_TESTE = teste_recepcao

SRC_TESTE = tests/teste_recepcao.c \
            src/atendimento/fila.c \
            src/atendimento/recepcao.c \
            src/escalonador/escalonador.c \
            src/input-output/teclado.c

$(EXEC_TESTE): $(SRC_TESTE)
	$(CC) $(CFLAGS) $(SRC_TESTE) -o $@ $(LDFLAGS)


test: $(EXEC_CLIENTE) $(EXEC_TESTE)
	./$(EXEC_TESTE)


# Execucao de exemplo
run: all
	./$(EXEC_ATENDIMENTO) 10 1000


# Limpeza
clean:
	rm -f $(OBJ_ATENDIMENTO) \
	      $(OBJ_ANALISTA) \
	      $(OBJ_CLIENTE) \
	      $(EXEC_ATENDIMENTO) \
	      $(EXEC_ANALISTA) \
	      $(EXEC_CLIENTE) \
	      $(EXEC_TESTE) \
	      demanda.txt \
	      lng.txt \
	      lng.tmp \
	      pid_analista.txt


.PHONY: all clean run test
