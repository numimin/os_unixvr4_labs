#define _POSIX_C_SOURCE 2

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

int print_upper(FILE* pout) {
    const size_t pipe_size = 4096;
    char* buf = malloc(pipe_size + 1);

    while (fgets(buf, pipe_size, pout)) {
        for (size_t i = 0; buf[i]; ++i) {
            if (islower(buf[i])) {
                buf[i] = toupper(buf[i]);
            }
        }
        printf("%s", buf);
    }

    if (ferror(pout)) {
        fprintf(stderr, "Couldn't read from pipe\n");
        free(buf);
        return EXIT_FAILURE;
    }

    free(buf);
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* filename = argv[1];

    const char* cmd = strcat(
        strcpy(
            malloc(sizeof("cat ") + strlen(filename)), 
            "cat "
        ),
        filename
    );
    FILE* pout = popen(cmd, "r");

    if (pout == NULL) {
        fprintf(stderr, "Couldn't open pipe\n");
        return EXIT_FAILURE;
    }

    pid_t pid;
    if ((pid = fork()) == -1) {
        perror("fork");
        pclose(pout);
        return EXIT_FAILURE;
    }

    if (!pid) {
        int res = print_upper(pout);
        pclose(pout);
        return res;
    }
    
    pclose(pout);

    while (wait(NULL) != -1)
        ;
    if (errno != ECHILD) {
        perror("wait");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
