#ifndef ENCERRAMENTO_H
#define ENCERRAMENTO_H

/* ===================================================================
   ENCERRAMENTO DO PROGRAMA  -  Pessoa 1

   O enunciado diz que o processo Atendimento "deve retornar a taxa de
   satisfacao (quantidade de clientes satisfeitos / pelo total de
   clientes) e o tempo total de execucao". Este modulo cuida disso.

   Nao depende de mais nada do projeto: quem contar os clientes so
   precisa passar os dois numeros no fim.

   Sobre a tecla 's' (caso N = 0): o enunciado manda calcular a taxa
   "levando em consideracao apenas os clientes que foram atendidos
   (ignora-se os que ficaram na fila)". Por isso a conta aqui divide
   por ATENDIDOS, e nao por clientes criados. Quem para pelo 's' nao e
   penalizado pela fila que sobrou.
   =================================================================== */

/* Marca o inicio da contagem. Chamar no comeco do main, antes de
   criar as threads.                                                 */
void encerramento_inicia(void);

/* Tempo decorrido desde encerramento_inicia(), em ms.               */
long encerramento_tempo_total_ms(void);

/* Taxa de satisfacao em porcentagem (0.0 a 100.0).
   Com 0 atendidos devolve 0.0, sem divisao por zero.                */
double encerramento_taxa(long atendidos, long satisfeitos);

/* Imprime o relatorio final na tela.                                */
void encerramento_relatorio(long atendidos, long satisfeitos);

#endif
