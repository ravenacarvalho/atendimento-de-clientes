# Compilador e Flags
CC = gcc
CFLAGS = -Wall -Wextra -g -Isrc -Isrc/atendimento -Isrc/escalonador -Isrc/input-output
LDFLAGS = -lrt

# Nomes dos executaveis
EXEC_ATENDIMENTO = atendimento
EXEC_ANALISTA = analista
EXEC_CLIENTE = cliente

# Localizacao dos fontes (.c)
SRC_ATENDIMENTO = $(wildcard src/atendimento/*.c src/escalonador/*.c src/input-output/*.c)
SRC_CLIENTE = cliente.c

# Objetos correspondentes (.o)
OBJ_ATENDIMENTO = $(SRC_ATENDIMENTO:.c=.o)
OBJ_CLIENTE = cliente.o

# Regra principal: compila tudo
all: $(EXEC_CLIENTE) $(EXEC_ATENDIMENTO)

# Executavel principal de Atendimento (com seu escalonador ucontext)
$(EXEC_ATENDIMENTO): $(OBJ_ATENDIMENTO)
	$(CC) $(OBJ_ATENDIMENTO) -o $@ $(LDFLAGS)

# Executavel do Cliente (fornecido no trabalho)
$(EXEC_CLIENTE): $(OBJ_CLIENTE)
	$(CC) $(OBJ_CLIENTE) -o $@ $(LDFLAGS)

# Regra generica para compilar qualquer .c em .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Testes da fila e da Recepcao (Pessoa 2). Usa o escalonador real, mas nao
# precisa do atendente nem do main do atendimento.
EXEC_TESTE = teste_recepcao
SRC_TESTE  = teste/teste_recepcao.c src/atendimento/fila.c \
             src/atendimento/recepcao.c \
             src/escalonador/escalonador.c src/input-output/teclado.c

$(EXEC_TESTE): $(SRC_TESTE) $(wildcard src/atendimento/*.h)
	$(CC) $(CFLAGS) $(SRC_TESTE) -o $@ $(LDFLAGS)

test: $(EXEC_CLIENTE) $(EXEC_TESTE)
	./$(EXEC_TESTE)

# Limpeza dos arquivos compilados e gerados
clean:
	rm -f $(OBJ_ATENDIMENTO) $(OBJ_CLIENTE) $(EXEC_ATENDIMENTO) $(EXEC_ANALISTA) $(EXEC_CLIENTE) $(EXEC_TESTE) demanda.txt LNG.txt

# Teste de execucao rapida (exemplo: N=10 clientes, paciencia X=1000ms)
run: all
	./$(EXEC_ATENDIMENTO) 10 1000

.PHONY: all clean run test