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
#include <aio.h>

#include "addresses.h"

#define LAB32_AIO_SIGNAL SIGUSR1

#define REMOVED_CLIENT (-1)

void debug(const char* s, int i) {
    static char buf[64];
    sprintf(buf, "%d\n", i);
    write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, " ", 1);
    write(STDOUT_FILENO, buf, strlen(buf));
}

struct server {
    sigset_t mask_aio;

    int sockfd;
    char* address_path;

    size_t client_count;
    struct aiocb clients[SOMAXCONN];

    size_t buf_size;
};

void cleanup(int sockfd, const char* socket_path) {
    close(sockfd);

    if (unlink(socket_path)) {
        perror("unlink");
    }
}

void safe_cleanup(struct server* this) {
    cleanup(this->sockfd, this->address_path);
    for (int i = 0; i < this->client_count; ++i) {
        if (this->clients[i].aio_fildes == REMOVED_CLIENT) continue;
        close(this->clients[i].aio_fildes);
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

int get_first_suitable_index(struct server* this) {
    int i = 0;
    while (this->clients[i].aio_fildes != REMOVED_CLIENT) ++i;
    return i;
}

void add_client(struct server* this, int fd) {
    sigprocmask(SIG_BLOCK, &this->mask_aio, NULL);

    const int index = get_first_suitable_index(this);

    if (index >= this->client_count) {
        this->clients[index].aio_buf = malloc(this->buf_size);
        ++this->client_count;
    }
    this->clients[index].aio_fildes = fd;

    aio_read(&this->clients[index]);

    sigprocmask(SIG_UNBLOCK, &this->mask_aio, NULL);
}

int init_server(struct server* this, const char* socket_path) {
    memset(this, 0, sizeof(*this));

    this->sockfd = server_setup(socket_path);
    if (this->sockfd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    this->address_path = malloc(strlen(socket_path) + 1);
    strcpy(this->address_path, socket_path);

    this->buf_size = BUFSIZ;
    for (size_t i = 0; i < SOMAXCONN; ++i) {
        this->clients[i].aio_fildes = REMOVED_CLIENT;

        this->clients[i].aio_nbytes = this->buf_size;

        this->clients[i].aio_sigevent.sigev_value.sival_int = i;
        this->clients[i].aio_sigevent.sigev_notify = SIGEV_SIGNAL;
        this->clients[i].aio_sigevent.sigev_signo = LAB32_AIO_SIGNAL;
    }

    sigemptyset(&this->mask_aio);
    sigaddset(&this->mask_aio, LAB32_AIO_SIGNAL);
    return EXIT_SUCCESS;
}

void cleanup_server(struct server* this) {
    safe_cleanup(this);
    for (size_t i = 0; i < this->client_count; ++i) {
        free((void*) this->clients[i].aio_buf);

        if (this->clients[i].aio_fildes == REMOVED_CLIENT) continue;
        close(this->clients[i].aio_fildes);
    }

    free(this->address_path);
}

static struct server server;

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&server);
    _exit(EXIT_SUCCESS);
}

void remove_client(struct server* this, size_t index) {
    if (index >= this->client_count) return;

    if (this->clients[index].aio_fildes == REMOVED_CLIENT) return;

    close(this->clients[index].aio_fildes);
    this->clients[index].aio_fildes = REMOVED_CLIENT;
}

#define DEBUG(S) write(STDERR_FILENO, (S), sizeof(S))

void aio_handler(int signum, siginfo_t *siginfo, void* vunused) {
    const size_t index = siginfo->si_value.sival_int;

    if (server.clients[index].aio_fildes == REMOVED_CLIENT) return;

    const int err = aio_error(&server.clients[index]);
    if (err == EINPROGRESS || err == ECANCELED) {
        return;
    }

    if (err > 0) {
        DEBUG("I/O Error\n");
        safe_cleanup(&server);
        _exit(EXIT_FAILURE);
    }

    const ssize_t count = aio_return(&server.clients[index]);

    if (count == 0) {
        remove_client(&server, index);
        return;
    }

    char* buf = (char*) server.clients[index].aio_buf;
    for (size_t i = 0; i < count; ++i) {
        if (islower(buf[i])) {
            buf[i] = toupper(buf[i]);
        }
    }
    write(STDOUT_FILENO, buf, count);
    aio_read(&server.clients[index]);
}

int main() {
    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_server(&server, SERVER_ADDR) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = aio_handler;
    act.sa_flags = SA_SIGINFO;
    sigaction(LAB32_AIO_SIGNAL, &act, NULL);

    for (;;) {
        const int client_fd = accept(server.sockfd, NULL, NULL);
        fprintf(stderr, "h0");
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            cleanup_server(&server);
            return EXIT_FAILURE;
        }

        add_client(&server, client_fd);
    }

    return EXIT_SUCCESS;
}
