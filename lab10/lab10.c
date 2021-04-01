#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    const pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("exec");
        return EXIT_FAILURE;
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        printf("Return code: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("Process was interrupted by signal %d\n", WTERMSIG(status));
    }

    return EXIT_SUCCESS;
}
