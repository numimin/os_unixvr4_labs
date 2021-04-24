#ifndef SERVER_MANAGEMENT_H
#define SERVER_MANAGEMENT_H

#include "iobuffer.h"
#include "socket_utils.h"

#include <poll.h>
#include <stddef.h>

#define REMOVED_CLIENT (-1)
#define NO_ID (-1)
#define MAX_CLIENTS 1024

#define POLL_LISTENER_INDEX 0
#define POLL_CLIENT_OFFSET 1

typedef struct {
    SocketAddress server_addr;

    struct pollfd clients[2 * MAX_CLIENTS + POLL_CLIENT_OFFSET];
    IOBuffer stoc_buffers[MAX_CLIENTS];
    IOBuffer ctos_buffers[MAX_CLIENTS];
    size_t client_count;

    int id_table[MAX_CLIENTS];
    int index_to_id[MAX_CLIENTS];
} Server;

typedef struct {
    SocketAddress listener_addr;
    SocketAddress server_addr;
} ProxyParams;

struct pollfd* get_client(Server* this, size_t id);
struct pollfd* get_server(Server* this, size_t id);
struct pollfd* get_listener(Server* this);

int init_server(Server* this, const ProxyParams* params);
void safe_cleanup(Server* this);
void cleanup_server(Server* this);

bool is_full(Server* this);
size_t get_client_count(Server* this);
size_t get_poll_count(Server* this);

int add_client(Server* this, int client_fd, int server_fd);
void remove_client(Server* this, size_t id);
void disconnect_client(Server* this, size_t id);

IOBuffer* get_ctos_buffer(Server* this, size_t id);
IOBuffer* get_stoc_buffer(Server* this, size_t id);

#endif // !SERVER_MANAGEMENT_H