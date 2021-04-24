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

#include "iobuffer.h"
#include "server_management.h"
#include "socket_utils.h"
#include "utils.h"

typedef struct {
    Server server;
    int id_table[MAX_CLIENTS];
    size_t client_count;
    size_t removed_count;
} ProxyServer;

static ProxyServer proxy_server;

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&proxy_server.server);
    _exit(EXIT_SUCCESS);
}

int can_read_from(struct pollfd* pollfd) {
    return pollfd->revents & POLLIN;
}

int can_write_to(struct pollfd* pollfd) {
    return pollfd->revents & POLLOUT;
}

int has_errors(struct pollfd* pollfd) {
    return pollfd->revents & POLLERR;
}

int is_ioable(struct pollfd* pollfd) {
    return can_read_from(pollfd) || can_write_to(pollfd) || has_errors(pollfd);
}

bool try_transfer(struct pollfd* sender, IOBuffer* buffer, struct pollfd* receiver) {
    if (!iob_full(buffer) && can_read_from(sender)) {
        const ssize_t count = iob_recv(buffer, sender->fd);
        if (count == -1) {
            return false;
        }
    }

    if (!iob_empty(buffer) && can_write_to(receiver)) {
        const ssize_t count = iob_send(buffer, receiver->fd);
        if (count == -1) {
            return false;
        }
    }

    return true;
}

void commit_remove(ProxyServer* proxy) {
    size_t removed = 0;
    size_t j = proxy->client_count - 1;

    for (size_t i = 0; i < j && removed < proxy->removed_count; ++i) {
        if (proxy->id_table[i] == REMOVED_CLIENT) {
            while (proxy->id_table[j] == REMOVED_CLIENT && j > i) {
                j--;
                removed++;
            }
            swap_int(&proxy->id_table[i], &proxy->id_table[j]);
            j--;
            removed++;
        }
    }

    proxy->client_count -= proxy->removed_count;
    proxy->removed_count = 0;
}

void pr_remove_client(ProxyServer* proxy, int i) {
    if (proxy->id_table[i] == REMOVED_CLIENT) return;

    remove_client(&proxy->server, proxy->id_table[i]);
    proxy->id_table[i] = REMOVED_CLIENT;
    proxy->removed_count++;
}

void try_read(ProxyServer* proxy, size_t ioable_count) {
    size_t ioable_processed = 0;

    Server* this = &proxy->server;

    for (size_t i = 0; ioable_processed < ioable_count && i < proxy->client_count; ++i) {
        const int id = proxy->id_table[i];

        struct pollfd* client = get_client(this, id);
        struct pollfd* server = get_server(this, id);

        if (is_ioable(client)) ioable_processed++;
        if (is_ioable(server)) ioable_processed++;

        if (has_errors(client) || has_errors(server)) {
            pr_remove_client(proxy, i);
            continue;
        }

        if (!try_transfer(client, get_ctos_buffer(this, id), server)) {
            pr_remove_client(proxy, i);
            continue;
        }

        if (!try_transfer(server, get_stoc_buffer(this, id), client)) {
            pr_remove_client(proxy, i);
            continue;
        }
    }
    commit_remove(proxy);
}

int main_loop(ProxyServer* proxy) {
    Server* this = &proxy->server;

    size_t fd_count;
    while ((fd_count = poll(this->clients, get_poll_count(this), -1)) != -1) {
        const int has_pending = get_listener(this)->revents & POLLIN;

        if (has_pending) {
            const int client_fd = accept(get_listener(this)->fd, NULL, NULL);

            if (client_fd == ERR_SOCKET) {
                continue;
            }

            const int server_fd = client_setup(&this->server_addr);
            if (server_fd == ERR_SOCKET) {
                close(client_fd);
                continue;        
            }

            const int id = add_client(this, client_fd, server_fd);
            if (id == NO_ID) {
                close(server_fd);
                close(client_fd);
                continue;
            }

            proxy->id_table[proxy->client_count++] = id;
        }

        try_read(proxy, has_pending ? fd_count - 1 : fd_count);
    }

    perror("poll");
    cleanup_server(this);
    return EXIT_FAILURE;
}

int parse_parameters(ProxyParams* this, int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s LISTENING_PORT IP_ADDR DESTINATION_PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_address(&this->server_addr, argv[2], argv[3]) == EXIT_FAILURE) {
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
    memcpy(&this->listener_addr.address, &addr_in, sizeof(addr_in));
    this->listener_addr.length = sizeof(addr_in);

    return EXIT_SUCCESS;
}

int pr_init_server(ProxyServer* this, const ProxyParams* params) {
    memset(this, 0, sizeof(*this));

    if (init_server(&this->server, params) == EXIT_FAILURE) return EXIT_FAILURE;

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        this->id_table[i] = REMOVED_CLIENT;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    ProxyParams params;
    if (parse_parameters(&params, argc, argv) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (pr_init_server(&proxy_server, &params) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    return main_loop(&proxy_server);
}
