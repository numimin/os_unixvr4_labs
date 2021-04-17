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

#define REMOVED_CLIENT (-1)

#define ERR_SOCKET (-1)

#define MAX_CLIENTS 255

typedef struct {
    struct sockaddr address;
    socklen_t length;
} SocketAddress;

void init_addr_in(struct sockaddr_in* addr_in, in_addr_t addr, in_port_t port, size_t family) {
    memset(addr_in, 0, sizeof(*addr_in));
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = port;
    addr_in->sin_family = family;
}

int server_setup(const SocketAddress* address) {
    const int sockfd = socket(address->address.sa_family, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return ERR_SOCKET;
    }

    if (bind(sockfd, &address->address, address->length)) {
        perror("bind");
        close(sockfd);
        return ERR_SOCKET;
    }

    if (listen(sockfd, MAX_CLIENTS)) {
        perror("listen");
        close(sockfd);
        return ERR_SOCKET;
    }

    return sockfd;
}

int client_setup(const SocketAddress* address) {
    const int sockfd = socket(address->address.sa_family, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return ERR_SOCKET;
    }

    if (connect(sockfd, &address->address, address->length)) {
        perror("connect");
        close(sockfd);
        return ERR_SOCKET;
    }

    return sockfd;
}

#define BUFFER_SIZE 1024
#define MESSAGE_SIZE (2 * BUFFER_SIZE + 3)

typedef struct {
    char* buf;
    size_t size;
    size_t count;
} IOBuffer;

#define NO_CLIENT_IDX (-1)

typedef struct {
    IOBuffer message;
    int client_index;
    size_t count;
    bool escape;
    bool order_put;
} MessageBuffer;

struct server {
    struct pollfd clients[MAX_CLIENTS + 2];
    IOBuffer tunnel_buffers[MAX_CLIENTS];
    int tunnel_client_index;
    MessageBuffer tunnel_mb;
    MessageBuffer client_mb;
    IOBuffer client_buffers[MAX_CLIENTS];
    size_t client_count;

    size_t clients_to_remove_count;
    size_t first_client_to_remove;
};

typedef struct {
    SocketAddress listener_addr;
    SocketAddress tunnel_addr;
} TunnelParams;

struct pollfd* get_client(struct server* this, size_t i) {
    return &this->clients[i + 2];
}

struct pollfd* get_listener(struct server* this) {
    return &this->clients[0];
}

struct pollfd* get_tunnel(struct server* this) {
    return &this->clients[1];
}

int is_full(struct server* this) {
    return this->client_count == MAX_CLIENTS;
}

int init_server(struct server* this, const TunnelParams* params) {
    memset(this, 0, sizeof(*this));

    const int listen_fd = server_setup(&params->listener_addr);
    if (listen_fd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    const int tunnel_fd = client_setup(&params->tunnel_addr);
    if (tunnel_fd == ERR_SOCKET) {
        close(listen_fd);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        get_client(this, i)->events = POLLIN | POLLOUT;
    }

    get_listener(this)->fd = listen_fd;
    get_listener(this)->events = POLLIN;

    get_tunnel(this)->fd = tunnel_fd;
    get_tunnel(this)->events = POLLIN | POLLOUT;

    init_mb(&this->tunnel_mb, MESSAGE_SIZE);
    init_mb(&this->client_mb, MESSAGE_SIZE);
    return EXIT_SUCCESS;
}

size_t get_client_count(struct server* this) {
    return this->client_count;
}

size_t get_poll_count(struct server* this) {
    return get_client_count(this) + 2;
}

void safe_cleanup(struct server* this) {
    close(get_listener(this)->fd);
    close(get_tunnel(this)->fd);
    
    for (size_t i = 0; i < get_client_count(this); ++i) {
        close(get_client(this, i)->fd);
    }
}

void free_iobuf(IOBuffer* this) {
    this->size = 0;
    this->count = 0;

    free(this->buf);
    this->buf = NULL;
}

void init_iobuf(IOBuffer* this, size_t size) {
    this->size = size;
    this->count = 0;
    this->buf = malloc(this->size);
}

void swap_size(size_t* lhs, size_t* rhs) {
    size_t* tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

void swap_ptr(void** lhs, void** rhs) {
    void* tmp = *lhs;
    *lhs = *rhs;
    *rhs = tmp;
}

void swap_iobuf(IOBuffer* lhs, IOBuffer* rhs) {
    swap_ptr(&lhs->buf, &rhs->buf);
    swap_size(&lhs->size, &rhs->size);
    swap_size(&lhs->count, &rhs->count);
}

void free_messagebuf(MessageBuffer* this) {
    free(&this->message);
}

void cleanup_server(struct server* this) {
    safe_cleanup(this);

    for (size_t i = 0; i < get_client_count(this); ++i) {
        free_iobuf(&this->client_buffers[i]);
        free_iobuf(&this->tunnel_buffers[i]);
    }

    free_messagebuf(&this->tunnel_mb);
    free_messagebuf(&this->client_mb);
}

static struct server server;

void interrupt(int unused) {
    write(STDERR_FILENO, "\nInterrupted. Exiting...\n", sizeof("Interrupted. Exiting...\n"));
    safe_cleanup(&server);
    _exit(EXIT_SUCCESS);
}

void remove_client(struct server* this, size_t i) {
    if (get_client(this, i)->fd == REMOVED_CLIENT) return;

    close(get_client(this, i)->fd);
    get_client(this, i)->fd = REMOVED_CLIENT;
    free_iobuf(&this->tunnel_buffers[i]);
    free_iobuf(&this->client_buffers[i]);

    if (this->clients_to_remove_count == 0 || this->first_client_to_remove > i) {
        this->first_client_to_remove = i;
    }
    this->clients_to_remove_count++;
}

void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

void swap_mb(MessageBuffer* this, size_t i, size_t j) {
    if (this->client_index == i) {
        this->client_index = j;
        return;
    }

    if (this->client_index == j) {
        this->client_index = i;
    }
}

void swap_clients(struct server* this, size_t i, size_t j) {
    swap(&get_client(this, i)->fd, &get_client(this, j)->fd);
    swap(&this->client_buffers[i], &this->client_buffers[j]);
    swap(&this->tunnel_buffers[i], &this->tunnel_buffers[j]);
    swap_mb(&this->client_mb, i, j);
    swap_mb(&this->tunnel_mb, i, j);
}

void commit_remove_clients(struct server* this) {
    if (this->clients_to_remove_count == 0) return;

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
}

int iob_full(const IOBuffer* this) {
    return this->count == this->size;
}

size_t iob_free_space(const IOBuffer* this) {
    return this->size - this->count;
}

ssize_t iob_read(IOBuffer* this, int fd) {
    const ssize_t free_space = iob_free_space(this);
    const ssize_t count = read(fd, &this->buf[this->count], free_space);
    if (count == -1) {
        perror("read");
        return count;
    }

    this->count += count;
    return count;
}

void iob_shift(IOBuffer* this, size_t offset) {
    if (offset == 0) return;

    this->count -= offset;
    for (size_t i = 0; i < this->count; ++i) {
        this->buf[i] = this->buf[offset + i];
    }
}

ssize_t iob_write(IOBuffer* this, int fd) {
    const ssize_t count = write(fd, this->buf, this->count);
    if (count == -1) {
        perror("write");
        return count;
    }

    iob_shift(this, count);
    return count;
}

void iob_puts(IOBuffer* this, const char* data, size_t count) {
    const size_t put_count = iob_free_space(this) > count ? count : iob_free_space(this);
    memcpy(&this->buf[this->count], data, put_count);
    this->count += put_count;
}

void iob_putc(IOBuffer* this, char c) {
    if (is_full(this)) return;
    this->buf[this->count++] = c;
}

void iob_clear(IOBuffer* this) {
    this->count = 0;
}

int can_read(struct pollfd* pollfd) {
    return pollfd->revents & POLLIN;
}

int can_write(struct pollfd* pollfd) {
    return pollfd->revents & POLLOUT;
}

void perform_client_io(struct server* this, size_t ioable_count) {
    size_t ioable_processed = 0;
    struct pollfd* tunnel = get_tunnel(this);
    if (can_read(tunnel) || can_write(tunnel)) {
        ioable_processed++;
    }

    for (size_t i = 0; ioable_processed < ioable_count && i < get_client_count(this); ++i) {
        struct pollfd* client = get_client(this, i);

        if (can_read(client) || can_write(client)) {
            ++ioable_processed;
        }

        //TODO: handle exceptional situations (closed socket) here, not by count==0

        if (can_read(client)) {
            if (iob_read(&this->client_buffers[i], client->fd) == -1) {
                remove_client(this, i);
                continue;
            }
        }

        if (can_write(client)) {
            if (iob_write(&this->tunnel_buffers[i], client->fd) == -1) {
                remove_client(this, i);
            }
        }
    }
    commit_remove_clients(this);
}

void init_mb(MessageBuffer* this, size_t size) {
    memset(this, 0, sizeof(*this));
    init_iobuf(&this->message, size);
    this->client_index = NO_CLIENT_IDX;
}

void add_client(struct server* this, int client_fd) {
    const size_t i = this->client_count++;

    get_client(this, i)->fd = client_fd;
    init_iobuf(&this->client_buffers[i], BUFFER_SIZE);
    init_iobuf(&this->tunnel_buffers[i], BUFFER_SIZE);
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

int parse_params(TunnelParams* this, int argc, char* argv[]) {
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

bool is_empty(IOBuffer* this) {
    return this->count == 0;
}

int next_message_client(struct server* this, int previous) {
    const int start = (previous == NO_CLIENT_IDX) ? 0 : (previous + 1) % this->client_count;
    const int end = (start >= 1) ? ((start - 1) % this->client_count) : (this->client_count - 1);

    int client;
    for (client = start; client != end; client = (client + 1) % this->client_count) {
        if (!is_empty(&this->client_buffers[client])) break;
    }

    if (client == end && is_empty(&this->client_buffers[end])) {
        return NO_CLIENT_IDX;
    }
    return client;
}

#define MESSAGE_EDGE 0x7E
#define MESSAGE_ESCAPE 0b01111101

bool msg_full(MessageBuffer* this) {
    return this->count == 0 || iob_full(&this->message);
}

char get_next_char(MessageBuffer* mb, IOBuffer* iob, int already_written) {
    if (mb->client_index == NO_CLIENT_IDX) {
        return MESSAGE_EDGE;
    }

    if (!mb->order_put) {
        return mb->client_index;
    }

    return iob->buf[already_written];
}

void encapsulate(struct server* this, MessageBuffer* mb, int client) {
    IOBuffer* iob = &this->client_buffers[client];
    IOBuffer* msg = &mb->message;

    if (mb->client_index == NO_CLIENT_IDX) {
        iob_putc(msg, get_next_char(mb, iob, 0));
        mb->client_index = client;
        mb->count = iob->count - 1;
        mb->order_put = false;
    }

    size_t written_count = 0;

    while (!msg_full(mb)) {
        const char c = get_next_char(mb, iob, written_count++);

        if (!mb->order_put) {
            written_count--;
        }

        if (mb->escape) {
            mb->escape = false;
        } else if (c == MESSAGE_EDGE || c == MESSAGE_ESCAPE) {
            iob_putc(msg, MESSAGE_ESCAPE);
            mb->count--;

            if (msg_full(mb)) {
                if (mb->order_put) {
                    mb->escape = true;
                }
                break;
            }
        }

        if (!mb->order_put) {
            mb->order_put = true;
        }

        iob_putc(msg, c);
        mb->count--;
    }

    if (!iob_full(msg)) {
        iob_putc(msg, MESSAGE_EDGE);
        mb->client_index = NO_CLIENT_IDX;
    }

    iob_shift(iob, written_count);
}

void fill_message(struct server* this) {
    //TODO: add control queue;
    MessageBuffer* mb = &this->tunnel_mb;

    this->tunnel_client_index = (mb->client_index == NO_CLIENT_IDX) ? next_message_client(this, this->tunnel_client_index) : mb->client_index;
    while (!iob_full(&mb->message) && this->tunnel_client_index != NO_CLIENT_IDX) {
        encapsulate(this, mb, this->tunnel_client_index);
        this->tunnel_client_index = (mb->client_index == NO_CLIENT_IDX) ? next_message_client(this, this->tunnel_client_index) : mb->client_index;
    }
}

void perform_protocol_io(struct server* this) {
    fill_message(this);
    if (can_write(get_tunnel(this))) {
        iob_write(&this->tunnel_mb.message, get_tunnel(this)->fd);
    }
}

int main_loop(struct server* this) {
    size_t fd_count;
    while ((fd_count = poll(this->clients, get_poll_count(this), -1)) != -1) {
        const int has_pending = get_listener(this)->revents & POLLIN;

        if (has_pending) {
            const int client_fd = accept(get_listener(this)->fd, NULL, NULL);

            if (client_fd == -1) {
                perror("accept");
                continue;
            }

            if (!is_full(this)) {
                add_client(this, client_fd);
            } else {
                close(client_fd);
            }
        }

        perform_client_io(&server, has_pending ? fd_count - 1 : fd_count);
        perform_protocol_io(&server);
    }

    perror("poll");
    cleanup_server(this);
    return EXIT_FAILURE;
}

int main(int argc, char* argv[]) {
    TunnelParams params;
    if (parse_params(&params, argc, argv) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    struct sigaction act = {};
    act.sa_handler = interrupt;
    sigaction(SIGINT, &act, NULL);

    if (init_server(&server, &params) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    return main_loop(&server);
}
