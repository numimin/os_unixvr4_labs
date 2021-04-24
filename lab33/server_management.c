#include "server_management.h"

#include "utils.h"

#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1024

#define CLIENT_INDEX(I) ((I) * 2)
#define SERVER_INDEX(I) ((I) * 2 + 1)

#define POLL_CLIENT_INDEX(I) (CLIENT_INDEX(I) + POLL_CLIENT_OFFSET)
#define POLL_SERVER_INDEX(I) (SERVER_INDEX(I) + POLL_CLIENT_OFFSET)

bool is_valid_id(Server* this, size_t id) {
    return id < MAX_CLIENTS && this->id_table[id] != REMOVED_CLIENT;
}

struct pollfd* get_client(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return NULL;
    return &this->clients[POLL_CLIENT_INDEX(this->id_table[id])];
}

struct pollfd* get_server(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return NULL;
    return &this->clients[POLL_SERVER_INDEX(this->id_table[id])];
}

struct pollfd* get_listener(Server* this) {
    return &this->clients[POLL_LISTENER_INDEX];
}

IOBuffer* get_ctos_buffer(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return NULL;
    return &this->ctos_buffers[id];
}

IOBuffer* get_stoc_buffer(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return NULL;
    return &this->stoc_buffers[id];
}

bool is_full(Server* this) {
    return this->client_count == MAX_CLIENTS;
}

int init_server(Server* this, const ProxyParams* params) {
    memset(this, 0, sizeof(*this));

    const int listen_fd = server_setup(&params->listener_addr, MAX_CLIENTS);
    if (listen_fd == ERR_SOCKET) {
        return EXIT_FAILURE;
    }

    memcpy(&this->server_addr, &params->server_addr, sizeof(this->server_addr));

    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        this->clients[POLL_CLIENT_INDEX(i)].fd = REMOVED_CLIENT;
        this->clients[POLL_CLIENT_INDEX(i)].events = POLLIN | POLLOUT;

        this->clients[POLL_SERVER_INDEX(i)].fd = REMOVED_CLIENT;
        this->clients[POLL_SERVER_INDEX(i)].events = POLLIN | POLLOUT;

        this->id_table[i] = REMOVED_CLIENT;
        this->index_to_id[i] = NO_ID;
    }

    get_listener(this)->fd = listen_fd;
    get_listener(this)->events = POLLIN;

    return EXIT_SUCCESS;
}

size_t get_client_count(Server* this) {
    return this->client_count;
}

size_t get_poll_count(Server* this) {
    return 2 * get_client_count(this) + POLL_CLIENT_OFFSET;
}

void safe_cleanup(Server* this) {
    close(get_listener(this)->fd);
    
    for (size_t i = 0; i < get_client_count(this); ++i) {
        close(get_client(this, this->index_to_id[i])->fd);
        close(get_server(this, this->index_to_id[i])->fd);
    }
}

void cleanup_server(Server* this) {
    safe_cleanup(this);

    for (size_t i = 0; i < get_client_count(this); ++i) {
        free_iobuf(get_ctos_buffer(this, this->index_to_id[i]));
        free_iobuf(get_stoc_buffer(this, this->index_to_id[i]));
    }
}

void swap_poll(struct pollfd* lhs, struct pollfd* rhs) {
    swap_int(&lhs->fd, &rhs->fd);
    swap_short(&lhs->revents, &rhs->revents);
    swap_short(&lhs->events, &rhs->events);
}

void disconnect_client(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return;
    if (get_client(this, id)->fd == REMOVED_CLIENT) return;

    close(get_client(this, id)->fd);
    close(get_server(this, id)->fd);
    get_client(this, id)->fd = REMOVED_CLIENT;
    get_server(this, id)->fd = REMOVED_CLIENT;
}

void remove_client(Server* this, size_t id) {
    if (!is_valid_id(this, id)) return;

    disconnect_client(this, id);

    free_iobuf(get_ctos_buffer(this, id));
    free_iobuf(get_stoc_buffer(this, id));

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

int add_client(Server* this, int client_fd, int server_fd) {
    if (client_fd < 0 || server_fd < 0) return NO_ID;

    const int id = next_id(this);
    if (id == NO_ID) return NO_ID;

    this->id_table[id] = this->client_count;
    this->index_to_id[this->client_count] = id;

    get_client(this, id)->fd = client_fd;
    get_server(this, id)->fd = server_fd;
    init_iobuf(get_stoc_buffer(this, id), BUFFER_SIZE);
    init_iobuf(get_ctos_buffer(this, id), BUFFER_SIZE);

    this->client_count++;
    return id;
}
