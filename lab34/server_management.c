#include "server_management.h"

#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define MESSAGE_SIZE (2 * BUFFER_SIZE + 3)

#define POLL_LISTENER_INDEX 0
#define POLL_TUNNEL_INDEX 1
#define POLL_CLIENT_OFFSET 2

struct pollfd* get_client(Server* this, size_t id) {
    if (id >= MAX_CLIENTS || this->id_table[id] == REMOVED_CLIENT) return NULL;

    return &this->clients[this->id_table[id] + POLL_CLIENT_OFFSET];
}

struct pollfd* get_listener(Server* this) {
    return &this->clients[POLL_LISTENER_INDEX];
}

struct pollfd* get_tunnel(Server* this) {
    return &this->clients[POLL_TUNNEL_INDEX];
}

bool is_full(Server* this) {
    return this->client_count == MAX_CLIENTS;
}

int init_server(Server* this, const TunnelParams* params) {
    memset(this, 0, sizeof(*this));

    const int listen_fd = server_setup(&params->listener_addr, MAX_CLIENTS);
    if (listen_fd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    const int tunnel_fd = client_setup(&params->tunnel_addr);
    if (tunnel_fd == ERR_SOCKET) {
        close(listen_fd);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        this->clients[POLL_CLIENT_OFFSET + i].fd = REMOVED_CLIENT;
        this->clients[POLL_CLIENT_OFFSET + i].events = POLLIN | POLLOUT;

        this->id_table[i] = REMOVED_CLIENT;
        this->index_to_id[i] = NO_ID;
    }

    get_listener(this)->fd = listen_fd;
    get_listener(this)->events = POLLIN;

    get_tunnel(this)->fd = tunnel_fd;
    get_tunnel(this)->events = POLLIN | POLLOUT;

    init_mb(&this->tunnel_mb, MESSAGE_SIZE);
    init_mb(&this->client_mb, MESSAGE_SIZE);
    return EXIT_SUCCESS;
}

size_t get_client_count(Server* this) {
    return this->client_count;
}

size_t get_poll_count(Server* this) {
    return get_client_count(this) + POLL_CLIENT_OFFSET;
}

void safe_cleanup(Server* this) {
    close(get_listener(this)->fd);
    close(get_tunnel(this)->fd);
    
    for (size_t i = 0; i < get_client_count(this); ++i) {
        close(get_client(this, this->index_to_id[i])->fd);
    }
}

void cleanup_server(Server* this) {
    safe_cleanup(this);

    for (size_t i = 0; i < get_client_count(this); ++i) {
        free_iobuf(&this->client_buffers[i]);
        free_iobuf(&this->tunnel_buffers[i]);
    }

    free_messagebuf(&this->tunnel_mb);
    free_messagebuf(&this->client_mb);
}

#define SWAP(T) void swap_##T (T* lhs, T* rhs) {T tmp = *lhs; *lhs = *rhs; *rhs = tmp;}

SWAP(int)
SWAP(short)

void swap_poll(struct pollfd* lhs, struct pollfd* rhs) {
    swap_int(&lhs->fd, &rhs->fd);
    swap_short(&lhs->revents, &rhs->revents);
    swap_short(&lhs->events, &rhs->events);
}

void disconnect_client(Server* this, size_t id) {
    if (get_client(this, id) == NULL) return;
    if (get_client(this, id)->fd == REMOVED_CLIENT) return;

    close(get_client(this, id)->fd);
    get_client(this, id)->fd = REMOVED_CLIENT;
}

void remove_client(Server* this, size_t id) {
    if (get_client(this, id) == NULL) return;

    disconnect_client(this, id);

    free_iobuf(get_tunnel_buf(this, id));
    free_iobuf(get_client_buf(this, id));

    this->client_count--;
    if (this->client_count != 0) {
        const int last_id = this->index_to_id[this->client_count];
        swap_poll(get_client(this, id), get_client(this, last_id));

        this->index_to_id[this->id_table[id]] = last_id;
        this->id_table[last_id] = this->id_table[id];
    }

    this->index_to_id[this->client_count] = NO_ID;
    this->id_table[id] = REMOVED_CLIENT;
}

int next_id(Server* this) {
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        if (this->id_table[i] == REMOVED_CLIENT) return i;
    }
    return NO_ID;
}

int add_client(Server* this, int client_fd) {
    const int id = next_id(this);
    if (id == NO_ID) return NO_ID;

    this->id_table[id] = this->client_count;
    this->index_to_id[this->client_count] = id;

    get_client(this, id)->fd = client_fd;
    init_iobuf(get_client_buf(this, id), BUFFER_SIZE);
    init_iobuf(get_tunnel_buf(this, id), BUFFER_SIZE);

    this->client_count++;
    return id;
}

IOBuffer* get_tunnel_buf(Server* this, size_t id) {
    return &this->tunnel_buffers[id];
}

IOBuffer* get_client_buf(Server* this, size_t id) {
    return &this->client_buffers[id];
}

void set_pollable_on(struct pollfd* pollfd, int flags, bool pollable) {
    if (pollable) {
        pollfd->events |= flags;
    } else {
        pollfd->events &= ~flags;
    }
}

void set_readable(struct pollfd* pollfd, bool readable) {
    set_pollable_on(pollfd, POLLIN, readable);
}

void set_writeable(struct pollfd* pollfd, bool writeable) {
    set_pollable_on(pollfd, POLLOUT, writeable);
}

void set_tunnel_writeable(Server* this, bool writeable) {
    set_writeable(get_tunnel(this), writeable);
}
