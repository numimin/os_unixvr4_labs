#include "multiplexer.h"

#define _GNU_SOURCE

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>

#define REMOVED_CLIENT (-1)

void remove_client(struct multiplexer* this, size_t index) {
    if (this->clients[index].fd == REMOVED_CLIENT) return;

    close(this->clients[index].fd);
    this->clients[index].fd = REMOVED_CLIENT;
    this->clients_to_remove_count++;

    if (this->first_client_to_remove == 0 || this->first_client_to_remove > index) {
        this->first_client_to_remove = index;
    }
}

void swap(int* a, int* b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

size_t get_client_count(struct multiplexer* this) {
    pthread_mutex_lock(&this->client_size_lock);
    size_t client_count = this->client_count;
    pthread_mutex_unlock(&this->client_size_lock);

    return client_count;
}

void inc_client_count(struct multiplexer* this, int delta) {
    pthread_mutex_lock(&this->client_size_lock);
    this->client_count += delta;
    pthread_mutex_unlock(&this->client_size_lock);
}

void commit_remove_clients(struct multiplexer* this) {
    if (!this->clients_to_remove_count) return;

    size_t removed_count = 0;

    size_t j = get_client_count(this) - 1;

    for (size_t i = this->first_client_to_remove; 
        removed_count < this->clients_to_remove_count && i < j; ++i) {

        if (this->clients[i].fd == REMOVED_CLIENT) {
            while (this->clients[j].fd == REMOVED_CLIENT && j > i) {
                --j;
                ++removed_count;
            }
            swap(&this->clients[i].fd, &this->clients[j].fd);
            --j;
            ++removed_count;
        }
    }
    
    inc_client_count(this, -this->clients_to_remove_count);

    this->clients_to_remove_count = 0;
    this->first_client_to_remove = 0;
}

void try_read(struct multiplexer* this, size_t readable_count) {
    const size_t local_client_count = get_client_count(this);

    size_t read_fd = 0;
    for (size_t i = 0; read_fd < readable_count && i < local_client_count; ++i) {
        if (this->clients[i].revents & POLLIN) {
            ++read_fd;
            
            ssize_t count;
            while ((count = read(this->clients[i].fd, this->buf, this->buf_size)) > 0) {
                for (size_t i = 0; i < count; ++i) {
                    if (islower(this->buf[i])) {
                        this->buf[i] = toupper(this->buf[i]);
                    }
                }
                write(STDOUT_FILENO, this->buf, count);
            }

            if (count == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("read");
                    remove_client(this, i);
                }
            }

            if (count == 0) {
                remove_client(this, i);
            }
        }
    }
    commit_remove_clients(this);
}

void read_cleanup(void* data) {
    struct multiplexer* muxer = (struct multiplexer*) data;

    free(muxer->buf);
    muxer->buf = NULL;
    muxer->buf_size = 0;
}

void read_from_clients(struct multiplexer* this) {
    this->buf = NULL;
    pthread_cleanup_push(read_cleanup, this);

    this->buf_size = BUFSIZ;
    this->buf = malloc(this->buf_size);

    size_t fd_count;
    const size_t local_client_count = get_client_count(this);
    while ((fd_count = poll(this->clients, local_client_count, -1)) != -1) {
        int errnum;
        if (!(errnum = pthread_mutex_trylock(&this->reading_lock))) {
            try_read(this, fd_count);
            pthread_mutex_unlock(&this->reading_lock);
            continue;
        }

        if (errnum == EBUSY) {
            pause();
        } else {
            fprintf(stderr, "pthread_mutex_trylock: %s\n", strerror(errnum));
            break;
        }
    }

    perror("poll");
    pthread_cleanup_pop(1);
}

void* thread_routine(void* data) {
    struct multiplexer* muxer = (struct multiplexer*) data;
    
    sigset_t intr_mask;
    sigemptyset(&intr_mask);
    sigaddset(&intr_mask, SIGINT);

    int errnum;
    if ((errnum = pthread_sigmask(SIG_BLOCK, &intr_mask, NULL))) {
        fprintf(stderr, "pthread_sigmask: %s\n", strerror(errnum));
    }

    read_from_clients(muxer);
}

int mx_start(struct multiplexer* this) {
    int errnum;

    if ((errnum = pthread_create(&this->reading_thread, NULL, thread_routine, this))) {
        fprintf(stderr, "pthread_create: %s\n", strerror(errnum));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void mx_init(struct multiplexer* this) {
    memset(this, 0, sizeof(*this));

    for (size_t i = 0; i < SOMAXCONN; ++i) {
        this->clients[i].events = POLLIN;
    }

    pthread_mutex_init(&this->client_size_lock, NULL);
    pthread_mutex_init(&this->reading_lock, NULL);
}

int restart_if_waits(struct multiplexer* this) {
    int errnum;
    if (!(errnum = pthread_mutex_trylock(&this->reading_lock))) {
        pthread_cancel(this->reading_thread);
        pthread_join(this->reading_thread, NULL);
        mx_start(this);

        pthread_mutex_unlock(&this->reading_lock);
        return EXIT_SUCCESS;
    }

    if (errnum != EBUSY) {
        fprintf(stderr, "pthread_mutex_trylock: %s\n", strerror(errnum));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int mx_add(struct multiplexer* this, int client_fd) {
    int old_flags = fcntl(client_fd, F_GETFD);
    if (old_flags == -1) {
        perror("fcntl");
        return EXIT_FAILURE;
    }

    if (fcntl(client_fd, F_SETFD, old_flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        return EXIT_FAILURE;
    }

    pthread_mutex_lock(&this->client_size_lock);
    this->clients[this->client_count++].fd = client_fd;
    pthread_mutex_unlock(&this->client_size_lock);

    return restart_if_waits(this);
}

void mx_cleanup(struct multiplexer* this) {
    pthread_cancel(this->reading_thread);
    pthread_join(this->reading_thread, NULL);

    for (size_t i = 0; i < this->client_count; ++i) {
        if (this->clients[i].fd != REMOVED_CLIENT) {
            close(this->clients[i].fd);
        }
    }

    pthread_mutex_destroy(&this->client_size_lock);

    memset(this, 0, sizeof(*this));
}
