#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        argv[0] = "cat";
        execvp("cat", argv);
        perror("execvp");
        return EXIT_FAILURE;
    }

    printf("-First line\n");
    fflush(stdout);

    if (waitpid(pid, NULL, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }

    printf("-Second line\n");

    return EXIT_SUCCESS;
}
