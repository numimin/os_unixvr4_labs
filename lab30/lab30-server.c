#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "addresses.h"

void print_upper(int fd) {
    const size_t pipe_size = 4096;
    char* buf = malloc(pipe_size);

    size_t count;
    while ((count = read(fd, buf, pipe_size))) {
        if (count == -1) {
            perror("read");
            return;
        }

        for (size_t i = 0; i < count; ++i) {
            if (islower(buf[i])) {
                buf[i] = toupper(buf[i]);
            }
        }
        
        write(STDOUT_FILENO, buf, count);
    }

    free(buf);
}

void cleanup(int sockfd) {
    close(sockfd);

    if (unlink(SERVER_ADDR)) {
        perror("unlink");
    }
}

int main() {
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

    if (bind(sockfd, cast_addr, sizeof(addr))){
        perror("bind");
        cleanup(sockfd);
        return EXIT_FAILURE;
    }

    if (listen(sockfd, 1)) {
        perror("listen");
        cleanup(sockfd);
        return EXIT_FAILURE;
    } 

    socklen_t addr_len;
    const int infd = accept(sockfd, cast_addr, &addr_len);
    cleanup(sockfd);

    if (infd == -1) {
        perror("accept");
        return EXIT_FAILURE;
    }

    print_upper(infd);
    close(infd);
    return EXIT_SUCCESS;
}
