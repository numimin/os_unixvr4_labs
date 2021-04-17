#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#define pin_fd pipefd[1]
#define pout_fd pipefd[0]

struct data {
    int pipefd[2];
    char* filename;
};

typedef int (*handler) (struct data*);

int close_pin(struct data* data) {
    close(data->pin_fd);
    return EXIT_SUCCESS;
}

int close_pout(struct data* data) {
    close(data->pout_fd);
    return EXIT_SUCCESS;
}

int write_file(struct data* data) {
    close(data->pout_fd);
    const int file = open(data->filename, O_RDONLY);
    if (file == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    const size_t pipe_size = fpathconf(data->pin_fd, _PC_PIPE_BUF);
    char* buf = malloc(pipe_size);

    size_t count;
    while ((count = read(file, buf, pipe_size))) {
        if (count == -1) {
            perror("read");
            free(buf);
            close(data->pin_fd);
            close(file);
            return EXIT_FAILURE;
        }

        write(data->pin_fd, buf, pipe_size);
    }

    free(buf);
    close(data->pin_fd);
    close(file);

    return EXIT_SUCCESS;
}

int print_upper(struct data* data) {
    close(data->pin_fd);

    const size_t pipe_size = fpathconf(data->pout_fd, _PC_PIPE_BUF);
    char* buf = malloc(pipe_size);

    size_t count;
    while ((count = read(data->pout_fd, buf, pipe_size))) {
        if (count == -1) {
            perror("read");
            free(buf);
            close(data->pout_fd);
            return EXIT_FAILURE;
        }

        for (size_t i = 0; i < count; ++i) {
            if (islower(buf[i])) {
                buf[i] = toupper(buf[i]);
            }
        }
        
        write(STDOUT_FILENO, buf, count);
    }

    free(buf);
    close(data->pout_fd);

    return EXIT_SUCCESS;
}

const static handler child_handlers[] = {
    write_file,
    print_upper
};

const static handler parent_handlers[] = {
    close_pin,
    close_pout
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct data data;
    if (pipe(data.pipefd)) {
        perror("pipe");
        return EXIT_FAILURE;
    }
    data.filename = argv[1];

    for (size_t i = 0; i < 2; ++i) {
        pid_t pid;
        if ((pid = fork()) == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (!pid) {
            return child_handlers[i](&data);
        }
        
        parent_handlers[i](&data);
    }

    while (wait(NULL) != -1)
        ;
    if (errno != ECHILD) {
        perror("wait");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
