#ifndef LAB_31_MULTIPLEXER_H
#define LAB_31_MULTIPLEXER_H

#include <stddef.h>
#include <pthread.h>
#include <sys/socket.h>
#include <poll.h>

struct multiplexer {
    pthread_t reading_thread;
    pthread_mutex_t client_size_lock;
    pthread_mutex_t reading_lock;

    char* buf;
    size_t buf_size;

    struct pollfd clients[SOMAXCONN];
    size_t client_count;

    size_t clients_to_remove_count;
    size_t first_client_to_remove;
};

void mx_init(struct multiplexer* this);
int mx_add(struct multiplexer* this, int client_fd);

int mx_start(struct multiplexer* this);
void mx_cleanup(struct multiplexer* this);

#endif // !LAB_31_MULTIPLEXER_H