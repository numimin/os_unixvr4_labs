#define _GNU_SOURCE //environ

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int execvpe(const char* path, char* const argv[], char* const envp[]);

int main(int argc, char* argv[]) {
    char* args[] = {"env", NULL};
    execvpe("env", args, &argv[1]);
    perror("exec");
    return EXIT_FAILURE;
}

int execvpe(const char* path, char* const argv[], char* const envp[]) {
    clearenv();

    for (; *envp; ++envp) {
        putenv(*envp);
    }
    return execvp(path, argv);
}
