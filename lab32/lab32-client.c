#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "addresses.h"

void write_file(int fd, const char* filename) {
    const int file = open(filename, O_RDONLY);

    const size_t pipe_size = BUFSIZ;
    char* buf = malloc(pipe_size);

    size_t count;
    while ((count = read(file, buf, pipe_size))) {
        write(fd, buf, count);
    }

    free(buf);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* filename = argv[1];

    const int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    struct sockaddr* cast_addr = (struct sockaddr*) &addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, SERVER_ADDR, sizeof(addr.sun_path));

    if (connect(sockfd, cast_addr, sizeof(addr))) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    write_file(sockfd, filename);
    close(sockfd);
    return EXIT_SUCCESS;
}
