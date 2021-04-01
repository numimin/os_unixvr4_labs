#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* filename = argv[1];

    const int fd = open(filename, O_WRONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    struct flock flock = {
        .l_start=0,
        .l_len=0,
        .l_whence=SEEK_SET,
        .l_type=F_WRLCK
    };

    if (fcntl(fd, F_SETLK, &flock) == -1) {
        perror("fcntl");
        close(fd);
        return EXIT_FAILURE;
    }

    const char* editor = "vim";
    char* cmd = malloc(strlen(editor) + 1 + strlen(filename) + 1);
    sprintf(cmd, "%s %s", editor, filename);
    
    if (system(cmd) == -1) {
        perror("fork");
        close(fd);
        return EXIT_FAILURE;
    }

    free(cmd);
    close(fd);
    return EXIT_SUCCESS;
}
