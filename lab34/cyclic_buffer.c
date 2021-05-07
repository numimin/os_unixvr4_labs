#include "cyclic_buffer.h"

#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void cb_free(CyclicBuffer* this) {
    free(this->buf);
    memset(this, 0, sizeof(*this));
}

void cb_init(CyclicBuffer* this, size_t size) {
    memset(this, 0, sizeof(*this));
    this->size = size;
    this->buf = malloc(this->size);
}

int cb_full(const CyclicBuffer* this) {
    return this->count == this->size;
}

bool cb_empty(const CyclicBuffer* this) {
    return this->count == 0;
}

size_t cb_free_space(const CyclicBuffer* this) {
    return this->size - this->count;
}

size_t cb_contiguous_space(const CyclicBuffer* this) {
    return min_size_t(this->size - this->start, this->count);
}

size_t cb_free_contiguous_space(const CyclicBuffer* this) {
    if (this->start + this->count <= this->size) {
        return this->size - (this->start + this->count);
    }
    return cb_free_space(this);
}

size_t cb_size(const CyclicBuffer* this) {
    return this->size;
}

size_t cb_count(const CyclicBuffer* this) {
    return this->count;
}

void cb_shift(CyclicBuffer* this) {
    if (this->start == 0) return;
    
    char* old_buf = memcpy(malloc(this->size), this->buf, this->size);
    for (size_t i = 0; i < this->count; ++i) {
        this->buf[i] = old_buf[(this->start + i) % this->size];
    }
    free(old_buf);

    this->start = 0;
}

bool cb_putc(CyclicBuffer* this, char c) {
    if (cb_full(this)) return false;

    this->buf[(this->start + this->count) % this->size] = c;
    cb_skip_right(this, 1);
    return true;
}

bool cb_peek(CyclicBuffer* this, char* c) {
    if (cb_empty(this)) return false;
    *c = this->buf[this->start];
    return true;
}

bool cb_getc(CyclicBuffer* this, char* c) {
    if (!cb_peek(this, c)) return false;
    cb_skip(this, 1);
    return true;
}

size_t cb_puts(CyclicBuffer* this, const char* s) {
    size_t i;
    for (i = 0; cb_putc(this, s[i]); i++)
        ;
    return i;
}

size_t cb_gets(CyclicBuffer* this, char* s) {
    size_t i;
    for (i = 0; cb_getc(this, &s[i]); i++)
        ;
    return i;
}

void cb_clear(CyclicBuffer* this) {
    this->start = 0;
    this->count = 0;
}

ssize_t cb_recv(CyclicBuffer* this, int fd) {
    cb_shift(this);

    const ssize_t free_space = cb_free_space(this);
    const ssize_t count = read(fd, &this->buf[this->count], free_space);
    if (count == -1) {
        perror("read");
        return count;
    }

    if (count == 0) {
        return -1;
    }

    cb_skip_right(this, count);
    return count;
}

ssize_t cb_send(CyclicBuffer* this, int fd) {
    if (cb_contiguous_space(this) != cb_count(this)) {
        cb_shift(this);
    }

    const ssize_t count = write(fd, &this->buf[this->start], this->count);
    if (count == -1) {
        perror("write");
        return count;
    }

    cb_skip(this, count);
    return count;
}

char* cb_data(CyclicBuffer* this) {
    return &this->buf[this->start];
}

char* cb_data_end(CyclicBuffer* this) {
    return &this->buf[(this->start + this->count) % this->size];
}

void cb_skip(CyclicBuffer* this, size_t count) {
    if (count > this->count) {
        count = this->count;
    }

    this->count -= count;
    this->start = (this->start + count) % this->size;
}

void cb_skip_right(CyclicBuffer* this, size_t count) {
    if (this->count + count > this->size) {
        count = this->size - this->count;
    }

    this->count += count;
}
