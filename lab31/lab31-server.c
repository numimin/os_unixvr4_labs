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
#include <signal.h>
#include <poll.h>

#include "addresses.h"

#define REMOVED_CLIENT (-1)

struct server {
    char* address_path;

    struct pollfd clients[SOMAXCONN];
    size_t client_count;

    char* buf;
    size_t buf_size;

    size_t clients_to_remove_count;
    size_t first_client_to_remove;
};

void cleanup(int sockfd, const char* path) {
    close(sockfd);

    if (unlink(path)) {
        perror("unlink");
    }
}

int get_sockfd(struct server* this) {
    return this->clients[0].fd;
}

void safe_cleanup(struct server* this) {
    cleanup(get_sockfd(this), this->address_path);
    for (int i = 1; i < this->client_count; ++i) {
        close(this->clients[i].fd);
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

int init_server(struct server* this, const char* socket_path) {
    memset(this, 0, sizeof(*this));

    const int sockfd = server_setup(socket_path);
    if (sockfd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    this->address_path = malloc(strlen(socket_path) + 1);
    strcpy(this->address_path, socket_path);

    for (size_t i = 0; i < SOMAXCONN; ++i) {
        this->clients[i].events = POLLIN;
    }

    this->clients[this->client_count++].fd = sockfd;

    this->buf_size = BUFSIZ;
    this->buf = malloc(this->buf_size);
    return EXIT_SUCCESS;
}

void cleanup_server(struct server* this) {
    cleanup(get_sockfd(this), this->address_path);
    free(this->buf);
    free(this->address_path);
}

static struct server server;

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&server);
    _exit(EXIT_SUCCESS);
}

void remove_client(struct server* this, size_t index) {
    if (index == 0) return;

    if (this->clients[index].fd == REMOVED_CLIENT) return;

    close(this->clients[index].fd);
    this->clients[index].fd = REMOVED_CLIENT;
    this->clients_to_remove_count++;

    if (this->first_client_to_remove == 0 || this->first_client_to_remove > index) {
        this->first_client_to_remove = index;
    }
}

void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void commit_remove_clients(struct server* this) {
    if (!this->clients_to_remove_count) return;
    if (!this->first_client_to_remove) return;

    size_t removed_count = 0;

    size_t j = this->client_count - 1;

    for (size_t i = this->first_client_to_remove; 
        removed_count < this->clients_to_remove_count && i < j; ++i) {

        if (this->clients[i].fd == REMOVED_CLIENT) {
            while (this->clients[j].fd == REMOVED_CLIENT && j > i) {
                --j;
                ++removed_count;
            }
            swap(&this->clients[i].fd, &this->clients[j].fd);
            --j;
            ++removed_count;
        }
    }
    
    this->client_count -= this->clients_to_remove_count;

    this->clients_to_remove_count = 0;
    this->first_client_to_remove = 0;
}

void try_read(struct server* this, size_t readable_count) {
    size_t read_fd = 0;
    for (size_t i = 1; read_fd <= readable_count && i < this->client_count; ++i) {
        if (this->clients[i].revents & POLLIN) {
            ++read_fd;
            
            const ssize_t count = read(this->clients[i].fd, this->buf, this->buf_size);
            for (size_t i = 0; i < count; ++i) {
                if (islower(this->buf[i])) {
                    this->buf[i] = toupper(this->buf[i]);
                }
            }
            write(STDOUT_FILENO, this->buf, count);

            if (count == -1) {
                perror("read");
                remove_client(this, i);
            }

            if (count == 0) {
                remove_client(this, i);
            }
        }
    }
    commit_remove_clients(this);
}

int mx_add(struct server* this, int client_fd) {
    this->clients[this->client_count++].fd = client_fd;
    return EXIT_SUCCESS;
}

int main() {
    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_server(&server, SERVER_ADDR) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    size_t fd_count;
    while ((fd_count = poll(server.clients, server.client_count, -1)) != -1) {
            
        const int has_pending = server.clients[0].revents & POLLIN;

        if (has_pending) {
            const int client_fd = accept(get_sockfd(&server), NULL, NULL);

            if (client_fd == -1) {
                perror("accept");
                cleanup_server(&server);
                return EXIT_FAILURE;
            }

            if (mx_add(&server, client_fd) != EXIT_SUCCESS) {
                cleanup_server(&server);
                return EXIT_FAILURE;
            }
        }

        try_read(&server, has_pending ? fd_count - 1 : fd_count);
    }

    perror("poll");
    cleanup_server(&server);
    return EXIT_SUCCESS;
}
