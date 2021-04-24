#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#define _GNU_SOURCE

#include <sys/socket.h>
#include <netinet/in.h>

#define ERR_SOCKET (-1)

typedef struct {
    struct sockaddr address;
    socklen_t length;
} SocketAddress;

int server_setup(const SocketAddress* address, int backlog);
int client_setup(const SocketAddress* address);

void init_addr_in(struct sockaddr_in* addr_in, in_addr_t addr, in_port_t port, size_t family);
int parse_address(SocketAddress* address, const char* addr_str, const char* port_str);

#endif // !SOCKET_UTILS_H