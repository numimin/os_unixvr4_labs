#ifndef CYCLIC_BUFFER_H
#define CYCLIC_BUFFER_H

#include <stddef.h>
#include <unistd.h>
#include <stdbool.h>

#define END_OF_BUFFER (-1)

typedef struct {
    char* buf;
    size_t size;
    size_t count;
    size_t start;
} CyclicBuffer;

void cb_free(CyclicBuffer* this);
void cb_init(CyclicBuffer* this, size_t size);

int cb_full(const CyclicBuffer* this);
bool cb_empty(const CyclicBuffer* this);
size_t cb_free_space(const CyclicBuffer* this);
size_t cb_contiguous_space(const CyclicBuffer* this);
size_t cb_free_contiguous_space(const CyclicBuffer* this);

size_t cb_size(const CyclicBuffer* this);
size_t cb_count(const CyclicBuffer* this);

bool cb_putc(CyclicBuffer* this, char c);
bool cb_getc(CyclicBuffer* this, char* c);

bool cb_peek(CyclicBuffer* this, char* c);

size_t cb_puts(CyclicBuffer* this, const char* s);
size_t cb_gets(CyclicBuffer* this, char* s);

void cb_clear(CyclicBuffer* this);

void cb_shift(CyclicBuffer* this);
ssize_t cb_recv(CyclicBuffer* this, int fd);
ssize_t cb_send(CyclicBuffer* this, int fd);

char* cb_data(CyclicBuffer* this);
char* cb_data_end(CyclicBuffer* this);

void cb_skip(CyclicBuffer* this, size_t count);
void cb_skip_right(CyclicBuffer* this, size_t count);

/*void reserve(IOBuffer* this, size_t free_space);*/

#endif // !CYCLIC_BUFFER_H
