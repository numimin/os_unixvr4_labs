#include "socket_utils.h"

#include <sys/types.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int server_setup(const SocketAddress* address, int backlog) {
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

    if (listen(sockfd, backlog)) {
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

void init_addr_in(struct sockaddr_in* addr_in, in_addr_t addr, in_port_t port, size_t family) {
    memset(addr_in, 0, sizeof(*addr_in));
    addr_in->sin_addr.s_addr = addr;
    addr_in->sin_port = port;
    addr_in->sin_family = family;
}

int parse_address(SocketAddress* address, const char* addr_str, const char* port_str) {
    struct addrinfo* addrinfo;

    const int err_code = getaddrinfo(addr_str, port_str, NULL, &addrinfo);
    if (err_code != 0) {
        fprintf(stderr, "getaddrinfo: %s", gai_strerror(err_code));
        return EXIT_FAILURE;
    }

    memcpy(&address->address, addrinfo->ai_addr, addrinfo->ai_addrlen);
    address->length = addrinfo->ai_addrlen;
    freeaddrinfo(addrinfo);

    return EXIT_SUCCESS;
}
