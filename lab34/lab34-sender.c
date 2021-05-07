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
#include <time.h>

#include "iobuffer.h"
#include "cyclic_buffer.h"
#include "socket_utils.h"
#include "server_management.h"
#include "transport_protocol_buffer.h"

#define BUFFER_SIZE 1024
#define MESSAGE_SIZE (2 * BUFFER_SIZE + 3)

typedef struct {
    Server server;
    int send_order;
    size_t send_count;

    int id_table[MAX_CLIENTS];
    bool remove_flag[MAX_CLIENTS];
    size_t client_count;

    CyclicBuffer add_queue;

    CyclicBuffer tunnel_buffers[MAX_CLIENTS];
    TPBuffer tunnel_tpb;
    CyclicBuffer client_buffers[MAX_CLIENTS];
} TunnelServer;

static TunnelServer tunnel_server;

int init_tserver(TunnelServer* this, const TunnelParams* params) {
    memset(this, 0, sizeof(*this));

    if (init_server(&this->server, params) == EXIT_FAILURE) return EXIT_FAILURE;

    tpb_init(&this->tunnel_tpb, MESSAGE_SIZE);
    cb_init(&this->add_queue, MAX_CLIENTS);

    return EXIT_SUCCESS;
}

void ts_remove_client(TunnelServer* this, int i) {
    const int id = this->id_table[i];
    disconnect_client(&this->server, id);
    this->remove_flag[id] = true;
}

int ts_add_client(TunnelServer* this, int fd) {    
    const int id = add_client(&this->server, fd);
    if (id == NO_ID) return EXIT_FAILURE;

    cb_init(&this->client_buffers[id], BUFFER_SIZE);
    cb_init(&this->tunnel_buffers[id], BUFFER_SIZE);

    cb_putc(&this->add_queue, (char) id);

    this->id_table[this->client_count++] = id;
    return EXIT_SUCCESS;
}

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&tunnel_server.server);
    _exit(EXIT_SUCCESS);
}

void perform_client_io(TunnelServer* this, size_t ioable_count) {
    Server* server = &this->server;

    size_t ioable_processed = 0;
    if (tunnel_ioable(server)) {
        ioable_processed++;
    }

    for (size_t i = 0; ioable_processed < ioable_count && i < this->client_count; ++i) {
        const int id = this->id_table[i];

        if (client_ioable(server, id)) {
            ++ioable_processed;
        }

        if (client_has_errors(server, id)) {
            ts_remove_client(this, i);
            continue;
        }

        if (!cb_full(&this->client_buffers[id]) && client_readable(server, id)) {
            const ssize_t count = cb_recv(&this->client_buffers[id], client_fd(server, id));
            if (count == -1) {
                ts_remove_client(this, i);
                continue;
            }
        }

        /*if (!iob_empty(&this->tunnel_buffers[id]) && can_write(client)) {
            const ssize_t sent = iob_send(&this->tunnel_buffers[id], client->fd);
            if (sent == -1) {
                ts_remove_client(this, i);
                continue;
            }
        }*/
    }
}

int next_message_client(TunnelServer* this, int previous) {
    if (this->client_count == 0) return NO_ORDER;

    Server* server = &this->server;

    const int start = (previous == NO_ORDER) ? 0 : (previous + 1) % this->client_count;
    const int end = (start >= 1) ? ((start - 1) % this->client_count) : (this->client_count - 1);

    int i;
    for (i = start; i != end; i = (i + 1) % this->client_count) {
        if (!cb_empty(&this->client_buffers[this->id_table[i]])) break;
    }

    if (i == end && cb_empty(&this->client_buffers[this->id_table[i]])) {
        return NO_ORDER;
    }
    return i;
}

void ts_actual_remove(TunnelServer* this, int i) {
    const int id = this->id_table[i];

    remove_client(&this->server, id);

    cb_free(&this->client_buffers[id]);
    cb_free(&this->tunnel_buffers[id]);

    this->id_table[i] = this->id_table[this->client_count - 1];
    this->client_count--;
}

bool process_empty_removed(TunnelServer* this) {
    TPBuffer* tpb = &this->tunnel_tpb;

    for (size_t i = 0; i < this->client_count; ) {
        const int id = this->id_table[i];
        
        if (!this->remove_flag[id] ||
            !cb_empty(&this->client_buffers[id])) {
                ++i;
                continue;
            }

        if (!cb_empty(&this->client_buffers[id])) continue;
        if (!tpb_contol_message(tpb, CLIENT_REMOVE, id)) return false;

        this->remove_flag[id] = false;
        ts_actual_remove(this, id);
    }

    return true;
}

bool process_added(TunnelServer* this) {
    TPBuffer* tpb = &this->tunnel_tpb;

    for (char id; cb_peek(&this->add_queue, &id); cb_skip(&this->add_queue, 1)) {
        if (!tpb_contol_message(tpb, CLIENT_ADD, id)) return false;
    }

    return true;
}

void fill_message(TunnelServer* this) {
    if (!process_empty_removed(this)) return;
    if (!process_added(this)) return;

    TPBuffer* tpb = &this->tunnel_tpb;

    do {
        if (this->send_order == NO_ORDER || this->send_count == 0) {
            this->send_order = next_message_client(this, this->send_order);
            if (this->send_order == NO_ORDER) break;
        }

        CyclicBuffer* data_buf = &this->client_buffers[this->id_table[this->send_order]];
        if (this->send_count == 0) {
            this->send_count = data_buf->count;
        }

        if (this->send_count == 0) break;

        cb_shift(data_buf);
        const ssize_t count = tpb_encapsulate(tpb, cb_data(data_buf), this->send_count, this->id_table[this->send_order]);
        if (count >= 0) {
            this->send_count -= count;
        }
        cb_skip(data_buf, count);
    } while (this->send_count == 0);
}

/*#define KB (1 << 10)
#define MB (KB << 10)

#define MAX_BUF_SIZE (MB)

void distribute_message(TunnelServer* this) {
    MessageReceiver* mb = &this->server.client_mb;

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

        const size_t decapsulated = decapsulate(mb, &buffer->buf[buffer->count], count);
        if (decapsulated < count) {
            fprintf(stderr, 
                "contiguous count and decapsulated count don't match!: (cont: %d, real: %d)\n",
                count, decapsulated);
        }
        buffer->count += decapsulated;

        if (buffer->size >= MAX_BUF_SIZE) {
            ts_remove_client(this, order);
        }
    }
}*/

void perform_protocol_io(TunnelServer* this) {
    Server* server = &this->server;

    fill_message(this);
    if (tunnel_writeable(server)) {
        tpb_send(&this->tunnel_tpb, tunnel_fd(server));
    }

    /*distribute_message(this);
    message = &server->client_mb.message;

    if (can_read(get_tunnel(server))) {
        iob_recv(message, get_tunnel(server)->fd);
    }*/
}

void ts_cleanup(TunnelServer* this) {
    cleanup_server(&this->server);

    for (size_t i = 0; i < get_client_count(&this->server); ++i) {
        cb_free(&this->client_buffers[this->id_table[i]]);
        cb_free(&this->tunnel_buffers[this->id_table[i]]);
    }

    tpb_free(&this->tunnel_tpb);
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

        set_tunnel_writeable(server, !tpb_empty(&this->tunnel_tpb));
        static const struct timespec sleep_time = {.tv_sec=0, .tv_nsec=10000000};
        nanosleep(&sleep_time, NULL);
    }

    perror("poll");
    ts_cleanup(this);
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
