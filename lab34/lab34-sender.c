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
#include <stdbool.h>

#include "iobuffer.h"
#include "socket_utils.h"
#include "message_buffer.h"
#include "server_management.h"

#define CONTROL_ORDER 255

#define CLIENT_ADD 1
#define CLIENT_REMOVE 2

#define EVENT_MESSAGE_LENGTH 2

typedef struct {
    Server server;
    int send_order;
    size_t send_count;

    int id_table[MAX_CLIENTS];
    bool remove_flag[MAX_CLIENTS];
    size_t client_count;

    int add_queue[MAX_CLIENTS];
    size_t event_start;
    size_t event_end;

    char event_message[EVENT_MESSAGE_LENGTH];
    size_t written_count;
} TunnelServer;

size_t mod_inc(size_t num, size_t mod) {
    return (num + 1) % mod;
}

static TunnelServer tunnel_server;

int init_tserver(TunnelServer* this, const TunnelParams* params) {
    memset(this, 0, sizeof(*this));
    this->written_count = EVENT_MESSAGE_LENGTH;

    if (init_server(&this->server, params) == EXIT_FAILURE) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

void ts_remove_client(TunnelServer* this, int i) {
    const int id = this->id_table[i];
    printf("remove client: %d\n", id);

    disconnect_client(&this->server, id);
    this->remove_flag[id] = true;
}

int ts_add_client(TunnelServer* this, int fd) {    
    const int id = add_client(&this->server, fd);
    printf("add client: %d\n", id);

    if (id == NO_ID) return EXIT_FAILURE;

    this->add_queue[this->event_end] = id;
    this->event_end = mod_inc(this->event_end, MAX_CLIENTS);

    this->id_table[this->client_count++] = id;
    return EXIT_SUCCESS;
}

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&tunnel_server.server);
    _exit(EXIT_SUCCESS);
}

int can_read(struct pollfd* pollfd) {
    return pollfd->revents & POLLIN;
}

int can_write(struct pollfd* pollfd) {
    return pollfd->revents & POLLOUT;
}

int has_errors(struct pollfd* pollfd) {
    return pollfd->revents & POLLERR;
}

void perform_client_io(TunnelServer* this, size_t ioable_count) {
    Server* server = &this->server;

    size_t ioable_processed = 0;
    struct pollfd* tunnel = get_tunnel(server);
    if (can_read(tunnel) || can_write(tunnel)) {
        ioable_processed++;
    }

    for (size_t i = 0; ioable_processed < ioable_count && i < this->client_count; ++i) {
        const int id = this->id_table[i];

        struct pollfd* client = get_client(server, id);

        if (can_read(client) || can_write(client)) {
            ++ioable_processed;
        }

        if (has_errors(client)) {
            ts_remove_client(this, i);
            continue;
        }

        if (!iob_full(get_client_buf(server, id)) && can_read(client)) {
            const ssize_t count = iob_recv(get_client_buf(server, id), client->fd);
            if (count == -1) {
                ts_remove_client(this, i);
                continue;
            }
        }

        if (!iob_empty(get_tunnel_buf(server, id)) && can_write(client)) {
            const ssize_t sent = iob_send(get_tunnel_buf(server, id), client->fd);
            if (sent == -1) {
                ts_remove_client(this, i);
                continue;
            }
        }
    }
}

int next_message_client(TunnelServer* this, int previous) {
    if (this->client_count == 0) return NO_ORDER;

    Server* server = &this->server;

    const int start = (previous == NO_ORDER) ? 0 : (previous + 1) % this->client_count;
    const int end = (start >= 1) ? ((start - 1) % this->client_count) : (this->client_count - 1);

    int client;
    for (client = start; client != end; client = (client + 1) % this->client_count) {
        if (!iob_empty(get_client_buf(server, this->id_table[client]))) break;
    }

    if (client == end && iob_empty(get_client_buf(server, this->id_table[client]))) {
        return NO_ORDER;
    }
    return client;
}

void fill_message_event(TunnelServer* this, int id, int op) {
    if (this->written_count != EVENT_MESSAGE_LENGTH) return;

    this->written_count = 0;
    this->event_message[0] = id;
    this->event_message[1] = op;
}

void fill_add_event(TunnelServer* this) {
    if (this->written_count != EVENT_MESSAGE_LENGTH) return;

    const int id = this->add_queue[this->event_start];
    this->event_start = mod_inc(this->event_start, MAX_CLIENTS);

    printf("actually adding: %d\n", id);

    fill_message_event(this, id, CLIENT_ADD);
}

bool fill_remove_event(TunnelServer* this) {
    if (this->written_count != EVENT_MESSAGE_LENGTH) return false;

    for (size_t i = 0; i < this->client_count; ++i) {
        const int id = this->id_table[i];
        if (!this->remove_flag[id]) continue;
        if (!iob_empty(get_client_buf(&this->server, id))) continue;

        this->remove_flag[id] = false;
        remove_client(&this->server, id);
        fill_message_event(this, id, CLIENT_REMOVE);

        this->id_table[i] = this->id_table[this->client_count--];
        printf("actually removing: %d\n", id);
        return true;
    }

    return false;
}

void fill_message(TunnelServer* this) {
    MessageBuffer* mb = &this->server.tunnel_mb;

    if (this->written_count != EVENT_MESSAGE_LENGTH) {
        const ssize_t count = encapsulate(mb, 
            &this->event_message[this->written_count], EVENT_MESSAGE_LENGTH - this->written_count, 
            CONTROL_ORDER);

        this->written_count += count;
        if (this->written_count != EVENT_MESSAGE_LENGTH) return;
    }

    while (fill_remove_event(this)) {
        const ssize_t count = encapsulate(mb, this->event_message, EVENT_MESSAGE_LENGTH, CONTROL_ORDER);

        this->written_count += count;
        if (this->written_count != EVENT_MESSAGE_LENGTH) return;
    }

    while (this->event_end != this->event_start) {
        fill_add_event(this);
        const ssize_t count = encapsulate(mb, this->event_message, EVENT_MESSAGE_LENGTH, CONTROL_ORDER);
        
        this->written_count += count;
        if (this->written_count != EVENT_MESSAGE_LENGTH) return;
    }

    do {
        if (this->send_order == NO_ORDER || this->send_count == 0) {
            this->send_order = next_message_client(this, this->send_order);
            if (this->send_order == NO_ORDER) break;
        }

        IOBuffer* data_buf = get_client_buf(&this->server, this->id_table[this->send_order]);
        if (this->send_count == 0) {
            this->send_count = data_buf->count;
        }

        if (this->send_count == 0) break;

        const ssize_t count = encapsulate(mb, data_buf->buf, this->send_count, this->id_table[this->send_order]);
        if (count >= 0) {
            this->send_count -= count;
            iob_shift(data_buf, count);
        }
    } while (this->send_count == 0);
}

#define KB (1 << 10)
#define MB (KB << 10)

#define MAX_BUF_SIZE (MB)

void distribute_message(TunnelServer* this) {
    MessageBuffer* mb = &this->server.tunnel_mb;

    int order;
    while ((order = get_current_order(mb)) != NO_ORDER) {
        const size_t count = get_contiguous_count(mb);

        if (order == CONTROL_ORDER) {
            //count == 1, that's obvious (no)
            char removed_id;
            decapsulate(mb, &removed_id, 1);
            ts_remove_client(this, removed_id);
            continue;
        }

        if (this->remove_flag[order]) {
            skip(mb, count);
        }

        IOBuffer* buffer = get_client_buf(&this->server, order);
        reserve(buffer, count);

        decapsulate(mb, &buffer->buf[buffer->count], count);
        buffer->count += count;

        if (buffer->size >= MAX_BUF_SIZE) {
            ts_remove_client(this, order);
        }
    }
}

void perform_protocol_io(TunnelServer* this) {
    Server* server = &this->server;

    fill_message(this);
    IOBuffer* message = &server->tunnel_mb.message;

    if (can_write(get_tunnel(server))) {
        iob_send(message, get_tunnel(server)->fd);
    }

    distribute_message(this);
    message = &server->client_mb.message;

    if (can_read(get_tunnel(server))) {
        iob_recv(message, get_tunnel(server)->fd);
    }
}

int main_loop(TunnelServer* this) {
    Server* server = &this->server;

    size_t fd_count;
    while ((fd_count = poll(server->clients, get_poll_count(server), -1)) != -1) {
        const int has_pending = get_listener(server)->revents & POLLIN;

        if (has_pending) {
            const int client_fd = accept(get_listener(server)->fd, NULL, NULL);

            if (client_fd == -1) {
                perror("accept");
                continue;
            }

            if (!is_full(server)) {
                ts_add_client(this, client_fd);
            } else {
                close(client_fd);
            }
        }

        perform_client_io(this, has_pending ? fd_count - 1 : fd_count);
        perform_protocol_io(this);
    }

    perror("poll");
    cleanup_server(server);
    return EXIT_FAILURE;
}

int parse_params(TunnelParams* this, int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s LISTENING_PORT IP_ADDR DESTINATION_PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_address(&this->tunnel_addr, argv[2], argv[3]) == EXIT_FAILURE) {
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

int main(int argc, char* argv[]) {
    TunnelParams params;
    if (parse_params(&params, argc, argv) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_tserver(&tunnel_server, &params) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    return main_loop(&tunnel_server);
}
