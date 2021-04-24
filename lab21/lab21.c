#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static sig_atomic_t beep_count = 0;

#define WRITE(S) {write(STDOUT_FILENO, (S), sizeof(S));}

static sigset_t sigint_mask;

void quit_handler(int signum) {
    sigprocmask(SIG_BLOCK, &sigint_mask, NULL);

    WRITE("I've beeped ");
    char buf[64];
    sprintf(buf, "%d", beep_count);
    write(STDOUT_FILENO, buf, strlen(buf));
    WRITE(" times\n");
    _exit(EXIT_SUCCESS);
}

void beep(int signum) {
    ++beep_count;
    write(STDOUT_FILENO, "\b", 1);
}

int main() {
    sigemptyset(&sigint_mask);
    sigaddset(&sigint_mask, SIGINT);

    struct sigaction act = {};
    act.sa_handler = beep,
    sigaction(SIGINT, &act, NULL);

    act.sa_handler = quit_handler;
    sigaction(SIGQUIT, &act, NULL);

    for (;;) {
        pause();
    }
    return EXIT_SUCCESS;
}
