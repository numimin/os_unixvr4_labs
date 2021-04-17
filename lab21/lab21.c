#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static int quit_flag = 0;
static sig_atomic_t beep_count = 0;

void quit_handler(int signum) {
    quit_flag = 1;
}

void beep(int signum) {
    ++beep_count;
    write(STDOUT_FILENO, "\b", 1);
}

int main() {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = beep,
    sigaction(SIGINT, &act, NULL);

    act.sa_handler = quit_handler;
    sigaction(SIGQUIT, &act, NULL);

    for (;;) {
        pause();
        if (quit_flag) {
            printf("I've beeped %d times\n", beep_count);
            break;
        }
    }
    return EXIT_SUCCESS;
}
