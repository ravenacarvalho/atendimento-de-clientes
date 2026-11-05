#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>

#include "teclado.h"

static struct termios cfg_antiga;
static int flags_antigas = 0;
static int ligado = 0;

/* se o usuario der Ctrl+C, precisamos devolver o terminal ao normal,
   senao ele fica sem eco e parece travado */
static void trata_sigint(int sig)
{
    (void)sig;
    teclado_restaura();
    _exit(1);
}

void teclado_init(void)
{
    struct termios nova;

    /* se a entrada nao for um terminal (ex: rodando com pipe), ignora */
    if (!isatty(STDIN_FILENO))
        return;

    if (tcgetattr(STDIN_FILENO, &cfg_antiga) != 0)
        return;

    nova = cfg_antiga;
    nova.c_lflag &= ~(ICANON | ECHO);   /* nao espera ENTER, nao ecoa */
    nova.c_cc[VMIN]  = 0;               /* read() nunca bloqueia      */
    nova.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &nova);

    flags_antigas = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags_antigas | O_NONBLOCK);

    ligado = 1;
    atexit(teclado_restaura);
    signal(SIGINT, trata_sigint);
}

void teclado_restaura(void)
{
    if (!ligado) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &cfg_antiga);
    fcntl(STDIN_FILENO, F_SETFL, flags_antigas);
    ligado = 0;
}

int teclado_tecla(void)
{
    unsigned char c;
    if (!ligado) return -1;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (int)c;
    return -1;
}
