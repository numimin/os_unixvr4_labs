#ifndef USUAL_SERVER_MANAGEMENT_H
#define USUAL_SERVER_MANAGEMENT_H

#include "socket_utils.h"

#include <poll.h>
#include <stddef.h>
#include <stdbool.h>

#define REMOVED_CLIENT (-1)
#define NO_ID (-1)
#define MAX_CLIENTS 1024

#define POLL_LISTENER_INDEX 0
#define POLL_CLIENT_OFFSET 1

typedef struct {
    struct pollfd clients[MAX_CLIENTS + POLL_CLIENT_OFFSET];
    size_t client_count;

    char* buf;
    size_t buf_size;

    int id_table[MAX_CLIENTS];
    int index_to_id[MAX_CLIENTS];
} Server;

typedef struct {
    SocketAddress listener_addr;
} ServerParams;

struct pollfd* get_client(Server* this, size_t id);
struct pollfd* get_listener(Server* this);

int init_server(Server* this, const ServerParams* params);
void safe_cleanup(Server* this);
void cleanup_server(Server* this);

bool is_full(Server* this);
size_t get_client_count(Server* this);
size_t get_poll_count(Server* this);

int add_client(Server* this, int client_fd);
void remove_client(Server* this, size_t id);
void disconnect_client(Server* this, size_t id);

#endif // !USUAL_SERVER_MANAGEMENT_H