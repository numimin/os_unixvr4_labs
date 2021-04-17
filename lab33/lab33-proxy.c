#define _GNU_SOURCE

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define REMOVED_CLIENT (-1)

#define ERR_SOCKET (-1)

#define MAX_SOCKETS 1024

void init_addr_in(struct sockaddr_in* addr_in, in_addr_t addr, in_port_t port, size_t family) {
    memset(addr_in, 0, sizeof(*addr_in));
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = port;
    addr_in->sin_family = family;
}

int server_setup(const struct sockaddr* addr, size_t addr_len) {
    const int sockfd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return ERR_SOCKET;
    }

    if (bind(sockfd, addr, addr_len)) {
        perror("bind");
        close(sockfd);
        return ERR_SOCKET;
    }

    if (listen(sockfd, MAX_SOCKETS)) {
        perror("listen");
        close(sockfd);
        return ERR_SOCKET;
    }

    return sockfd;
}

int server_conn_setup(const struct sockaddr* addr, size_t addr_len) {
    const int sockfd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return ERR_SOCKET;
    }

    /*int retries = 2;
    setsockopt(sockfd, SOL_TCP, TCP_SYNCNT, &retries, sizeof(retries));*/

    if (connect(sockfd, addr, addr_len)) {
        perror("connect");
        close(sockfd);
        return ERR_SOCKET;
    }

    return sockfd;
}

struct server {
    struct sockaddr server_addr;
    size_t server_addr_len;

    struct pollfd clients[2 * MAX_SOCKETS];
    size_t client_count;

    char* buf;
    size_t buf_size;

    size_t clients_to_remove_count;
    size_t first_client_to_remove;
};

typedef struct {
    struct sockaddr listen_addr;
    size_t listen_addr_len;
    struct sockaddr dest_addr;
    size_t dest_addr_len;
} ProxyParameters;

struct pollfd* get_client(struct server* this, size_t i) {
    return &this->clients[2 * i];
}

struct pollfd* get_server(struct server* this, size_t i) {
    return &this->clients[2 * i + 1];
}

struct pollfd* get_proxy(struct server* this) {
    return &this->clients[0];
}

int init_server(struct server* this, const ProxyParameters* params) {
    memset(this, 0, sizeof(*this));

    const int proxyfd = server_setup(&params->listen_addr, params->listen_addr_len);
    if (proxyfd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    this->server_addr_len = params->dest_addr_len;
    memcpy(&this->server_addr, &params->dest_addr, this->server_addr_len);

    for (size_t i = 0; i < MAX_SOCKETS; ++i) {
        get_client(this, i)->events = POLLIN | POLLOUT;
        get_server(this, i)->events = POLLIN | POLLOUT;
    }

    get_proxy(this)->fd = proxyfd;
    get_server(this, 0)->fd = REMOVED_CLIENT;
    this->client_count++;

    this->buf_size = BUFSIZ;
    this->buf = malloc(this->buf_size);
    return EXIT_SUCCESS;
}

int get_serverfd(struct server* this) {
    return get_proxy(this)->fd;
}

void safe_cleanup(struct server* this) {
    close(get_serverfd(this));
    for (int i = 1; i < this->client_count; ++i) {
        close(get_client(this, i)->fd);
        close(get_server(this, i)->fd);
    }
}

void cleanup_server(struct server* this) {
    safe_cleanup(this);
    free(this->buf);
}

static struct server server;

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&server);
    _exit(EXIT_SUCCESS);
}

void remove_client(struct server* this, size_t index) {
    if (index == 0) return;

    if (get_client(this, index)->fd == REMOVED_CLIENT) return;

    close(get_client(this, index)->fd);
    close(get_server(this, index)->fd);
    get_client(this, index)->fd = REMOVED_CLIENT;
    get_server(this, index)->fd = REMOVED_CLIENT;
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

void swap_clients(struct server* this, size_t i, size_t j) {
    swap(&get_client(this, i)->fd, &get_client(this, j)->fd);
    swap(&get_server(this, i)->fd, &get_server(this, j)->fd);
}

void commit_remove_clients(struct server* this) {
    if (!this->clients_to_remove_count) return;
    if (!this->first_client_to_remove) return;

    size_t removed_count = 0;

    size_t j = this->client_count - 1;

    for (size_t i = this->first_client_to_remove; 
        removed_count < this->clients_to_remove_count && i < j; ++i) {

        if (get_client(this, i)->fd == REMOVED_CLIENT) {
            while (get_client(this, j)->fd == REMOVED_CLIENT && j > i) {
                --j;
                ++removed_count;
            }
            swap_clients(this, i, j);
            --j;
            ++removed_count;
        }
    }
    
    this->client_count -= this->clients_to_remove_count;

    this->clients_to_remove_count = 0;
    this->first_client_to_remove = 0;
}

int copy(struct server* this, int infd, int outfd) {
    const ssize_t rcount = read(infd, this->buf, this->buf_size);
    if (rcount == -1) {
        perror("read");
        return EXIT_FAILURE;
    }
    if (rcount == 0) {
        return EXIT_FAILURE;
    }

    const ssize_t wcount = write(outfd, this->buf, rcount);
    if (wcount == -1) {
        perror("write");
        return EXIT_FAILURE;
    }
    if (wcount == 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int can_read_client(struct server* this, size_t i) {
    return get_client(this, i)->revents & POLLIN;
}

int can_read_server(struct server* this, size_t i) {
    return get_server(this, i)->revents & POLLIN;
}

int can_write_server(struct server* this, size_t i) {
    return get_server(this, i)->revents & POLLOUT;
}

int can_write_client(struct server* this, size_t i) {
    return get_client(this, i)->revents & POLLOUT;
}

void try_read(struct server* this, size_t ioable_count) {
    size_t ioable_fd = 0;
    for (size_t i = 1; ioable_fd < ioable_count && i < this->client_count; ++i) {
        if (can_read_client(this, i) || can_write_client(this, i)) ++ioable_fd;
        if (can_write_server(this, i) || can_read_server(this, i)) ++ioable_fd;

        if (can_write_server(this, i) && can_read_client(this, i)) {
            if (copy(this, get_client(this, i)->fd, get_server(this, i)->fd) == EXIT_FAILURE) {
                remove_client(this, i);
                continue;
            }
        }
        
        if (can_write_client(this, i) && can_read_server(this, i)) {
            if (copy(this, get_server(this, i)->fd, get_client(this, i)->fd) == EXIT_FAILURE) {
                remove_client(this, i);
                continue;
            }
        }
    }
    commit_remove_clients(this);
}

void mx_add(struct server* this, int client_fd, int server_fd) {
    get_client(this, this->client_count)->fd = client_fd;
    get_server(this, this->client_count)->fd = server_fd;
    printf("Proxy: added %d\n", this->client_count);
    ++this->client_count;
}

int parse_address(const char* addr_str, const char* port_str, struct sockaddr* addr, size_t* addrlen) {
    struct addrinfo* addrinfo;
    if (getaddrinfo(addr_str, port_str, NULL, &addrinfo) == 0) {
        memcpy(addr, addrinfo->ai_addr, addrinfo->ai_addrlen);
        *addrlen = addrinfo->ai_addrlen;
        freeaddrinfo(addrinfo);
        return EXIT_SUCCESS;
    }

    perror("getaddrinfo");    
    return EXIT_FAILURE;
}

int parse_parameters(ProxyParameters* this, int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s LISTENING_PORT IP_ADDR DESTINATION_PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_address(argv[2], argv[3], &this->dest_addr, &this->dest_addr_len) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    char* end;
    const in_port_t listen_port = strtol(argv[1], &end, 10);
    if (*end != '\0' || listen_port < 0) {
        fprintf(stderr, "LISTENING_PORT must be a positive integer\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr_in;
    init_addr_in(&addr_in, htonl(INADDR_LOOPBACK), htons(listen_port), AF_INET);
    memcpy(&this->listen_addr, &addr_in, sizeof(addr_in));
    this->listen_addr_len = sizeof(addr_in);

    return EXIT_SUCCESS;
}

int main_loop(struct server* this) {
    size_t fd_count;
    while ((fd_count = poll(this->clients, 2 * this->client_count, -1)) != -1) {
        const int has_pending = get_proxy(this)->revents & POLLIN;

        if (has_pending) {
            const int client_fd = accept(get_serverfd(this), NULL, NULL);

            if (client_fd == -1) {
                perror("accept");
                continue;
            }

            const int server_fd = server_conn_setup(&this->server_addr, this->server_addr_len);
            if (server_fd == ERR_SOCKET) {
                close(client_fd);
                continue;        
            }

            mx_add(this, client_fd, server_fd);
        }

        try_read(&server, has_pending ? fd_count - 1 : fd_count);
    }

    perror("poll");
    cleanup_server(this);
    return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
    ProxyParameters parameters;
    if (parse_parameters(&parameters, argc, argv) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_server(&server, &parameters) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    return main_loop(&server);
}
