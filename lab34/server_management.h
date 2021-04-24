#ifndef SERVER_MANAGEMENT_H
#define SERVER_MANAGEMENT_H

#include "iobuffer.h"
#include "message_buffer.h"
#include "socket_utils.h"

#include <poll.h>
#include <stddef.h>

#define NO_ORDER (-1)
#define REMOVED_CLIENT (-1)
#define NO_ID (-1)
#define MAX_CLIENTS 255

typedef struct {
    struct pollfd clients[MAX_CLIENTS + 2];
    IOBuffer tunnel_buffers[MAX_CLIENTS];
    int tunnel_client_index;
    MessageBuffer tunnel_mb;
    MessageBuffer client_mb;
    IOBuffer client_buffers[MAX_CLIENTS];
    size_t client_count;

    int id_table[MAX_CLIENTS];
    int index_to_id[MAX_CLIENTS];
} Server;

typedef struct {
    SocketAddress listener_addr;
    SocketAddress tunnel_addr;
} TunnelParams;

struct pollfd* get_client(Server* this, size_t id);
struct pollfd* get_listener(Server* this);
struct pollfd* get_tunnel(Server* this);

int init_server(Server* this, const TunnelParams* params);
void safe_cleanup(Server* this);
void cleanup_server(Server* this);

bool is_full(Server* this);
size_t get_client_count(Server* this);
size_t get_poll_count(Server* this);

int add_client(Server* this, int client_fd);
void remove_client(Server* this, size_t id);
void disconnect_client(Server* this, size_t id);

IOBuffer* get_tunnel_buf(Server* this, size_t id);
IOBuffer* get_client_buf(Server* this, size_t id);

void set_tunnel_writeable(Server* this, bool readable);

/*void set_readable(Server* this, size_t id, bool readable);
void set_writeable(Server* this, size_t id, bool readable);*/

#endif // !SERVER_MANAGEMENT_H