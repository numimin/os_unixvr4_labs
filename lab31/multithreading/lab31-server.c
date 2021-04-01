#define _GNU_SOURCE

#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>

#include "addresses.h"
#include "multiplexer.h"

void cleanup(int sockfd, const char* socket_path) {
    close(sockfd);

    if (unlink(socket_path)) {
        perror("unlink");
    }
}

#define ERR_SOCKET (-1)

int server_setup(const char* socket_path) {
    const int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return ERR_SOCKET;
    }

    struct sockaddr_un addr;
    struct sockaddr* addrp = (struct sockaddr*) &addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path));

    if (bind(sockfd, addrp, sizeof(addr))){
        perror("bind");
        cleanup(sockfd, socket_path);
        return ERR_SOCKET;
    }

    if (listen(sockfd, SOMAXCONN)) {
        perror("listen");
        cleanup(sockfd, socket_path);
        return ERR_SOCKET;
    }

    return sockfd;
}

struct server {
    int sockfd;
    char* address_path;

    struct multiplexer muxer;
};

int init_server(struct server* this, const char* socket_path) {
    memset(this, 0, sizeof(*this));

    this->sockfd = server_setup(socket_path);
    if (this->sockfd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    this->address_path = malloc(strlen(socket_path) + 1);
    strcpy(this->address_path, socket_path);

    mx_init(&this->muxer);
    return EXIT_SUCCESS;
}

void cleanup_server(struct server* this) {
    mx_cleanup(&this->muxer);
    cleanup(this->sockfd, this->address_path);
    free(this->address_path);
}

static int quit_flag = 0;
void interrupt(int unused) {
    quit_flag = 1;
}

static struct server server;

int main() {
    struct sigaction act;
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_server(&server, SERVER_ADDR) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    if (mx_start(&server.muxer) != EXIT_SUCCESS) {
        cleanup_server(&server);
        return EXIT_FAILURE;
    }

    while (!quit_flag) {
        const int client_fd = accept(server.sockfd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EINTR) {
                cleanup_server(&server);
                return EXIT_SUCCESS;
            }

            perror("accept");
            return EXIT_FAILURE;
        }

        if (mx_add(&server.muxer, client_fd) != EXIT_SUCCESS) {
            cleanup_server(&server);
            return EXIT_FAILURE;
        }
    }

    cleanup_server(&server);
    return EXIT_SUCCESS;
}
