#ifndef IOBUFFER_H
#define IOBUFFER_H

#include <stddef.h>
#include <unistd.h>
#include <stdbool.h>

#define END_OF_BUFFER (-1)

typedef struct {
    char* buf;
    size_t size;
    size_t count;
} IOBuffer;

void free_iobuf(IOBuffer* this);
void init_iobuf(IOBuffer* this, size_t size);

int iob_full(const IOBuffer* this);
bool iob_empty(const IOBuffer* this);
size_t iob_free_space(const IOBuffer* this);

ssize_t iob_recv(IOBuffer* this, int fd);
void iob_shift(IOBuffer* this, size_t offset);
ssize_t iob_send(IOBuffer* this, int fd);

size_t iob_puts(IOBuffer* this, const char* data, size_t count);
bool iob_putc(IOBuffer* this, char c);
void iob_clear(IOBuffer* this);

int iob_getc(IOBuffer* this);

void reserve(IOBuffer* this, size_t free_space);

#endif // !IOBUFFER_H
