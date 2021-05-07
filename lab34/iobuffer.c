#include "iobuffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void free_iobuf(IOBuffer* this) {
    this->size = 0;
    this->count = 0;

    free(this->buf);
    this->buf = NULL;
}

void init_iobuf(IOBuffer* this, size_t size) {
    this->size = size;
    this->count = 0;
    this->buf = malloc(this->size);
}

int iob_full(const IOBuffer* this) {
    return this->count == this->size;
}

size_t iob_free_space(const IOBuffer* this) {
    return this->size - this->count;
}

ssize_t iob_recv(IOBuffer* this, int fd) {
    const ssize_t free_space = iob_free_space(this);
    const ssize_t count = read(fd, &this->buf[this->count], free_space);
    if (count == -1) {
        perror("read");
        return count;
    }

    if (count == 0) {
        return -1;
    }

    this->count += count;
    return count;
}

void iob_shift(IOBuffer* this, size_t offset) {
    if (offset == 0) return;

    if (offset > this->count) {
        offset = this->count;
    }

    this->count -= offset;
    for (size_t i = 0; i < this->count; ++i) {
        this->buf[i] = this->buf[offset + i];
    }
}

ssize_t iob_send(IOBuffer* this, int fd) {
    const ssize_t count = write(fd, this->buf, this->count);
    if (count == -1) {
        perror("write");
        return count;
    }

    iob_shift(this, count);
    return count;
}

size_t iob_puts(IOBuffer* this, const char* data, size_t count) {
    const size_t put_count = iob_free_space(this) > count ? count : iob_free_space(this);
    memcpy(&this->buf[this->count], data, put_count);
    this->count += put_count;

    return put_count;
}

bool iob_putc(IOBuffer* this, char c) {
    if (iob_full(this)) return false;

    this->buf[this->count++] = c;
    return true;
}

int iob_getc(IOBuffer* this) {
    if (iob_empty(this)) return END_OF_BUFFER;
    const char c = this->buf[0];
    iob_shift(this, 1);
    return c;
}

void iob_clear(IOBuffer* this) {
    this->count = 0;
}

bool iob_empty(const IOBuffer* this) {
    return this->count == 0;
}

void resize(IOBuffer* this, size_t new_size) {
    this->buf = realloc(this->buf, new_size);
}

void reserve(IOBuffer* this, size_t free_space) {
    if (iob_free_space(this) >= free_space) return;

    const size_t new_size = iob_free_space(this) - free_space + this->size; 
    resize(this, new_size);    
}
