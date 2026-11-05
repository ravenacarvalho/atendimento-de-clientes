#ifndef TECLADO_H
#define TECLADO_H

/* Leitura de tecla sem bloquear e sem precisar de ENTER.
   Usado pelo escalonador para detectar a tecla 's'.
   Responsavel: Pessoa 1 */

void teclado_init(void);      /* coloca o terminal em modo cru        */
void teclado_restaura(void);  /* devolve o terminal ao normal         */
int  teclado_tecla(void);     /* retorna a tecla ou -1 se nao houver  */

#endif
