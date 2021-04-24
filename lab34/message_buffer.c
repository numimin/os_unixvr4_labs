#include "message_buffer.h"

#include <string.h>

#define MESSAGE_EDGE 0x7E
#define MESSAGE_ESCAPE 0b01111101

void free_messagebuf(MessageBuffer* this) {
    free_iobuf(&this->message);
}

void init_mb(MessageBuffer* this, size_t size) {
    memset(this, 0, sizeof(*this));
    init_iobuf(&this->message, size);
}

bool mb_full(const MessageBuffer* this) {
    return iob_full(&this->message);
}

bool try_put(MessageBuffer* this, char c) {
    if (mb_full(this)) return false;
    iob_putc(&this->message, c);
    return true;
}

bool put_escaped_char(MessageBuffer* this) {
    if (this->escape) {
        if (!try_put(this, this->escaped_char)) return false;
        this->escape = false;
    }

    return true;
}

bool put_start(MessageBuffer* this) {
    if (this->start_put) return true;

    if (!try_put(this, MESSAGE_EDGE)) return false;
    this->start_put = true;

    return true;
}

bool put_end(MessageBuffer* this) {
    if (!this->started) return true;

    if (!try_put(this, MESSAGE_EDGE)) return false;
    this->started = false;
    
    return true;
}

bool put_order(MessageBuffer* this) {
    if (this->order_put) return true;

    if (!try_put(this, this->order)) return false;
    this->order_put = true;

    return true;
}

bool need_escape(char c) {
    return c == MESSAGE_ESCAPE || c == MESSAGE_EDGE;
}

bool put_char(MessageBuffer* this, char c) {
    if (!put_escaped_char(this)) return false;

    if (need_escape(c)) {
        if (!try_put(this, MESSAGE_ESCAPE)) return false;
        this->escape = true;
        this->escaped_char = c;
    } else {
        if (!try_put(this, c)) return false;
    }

    return true;
}

#include <stdio.h>

ssize_t encapsulate(MessageBuffer* this, const char* data, size_t count, char order) {
    if (this->started && this->order != order) {
        if (!put_escaped_char(this)) return NO_SPACE;
        if (!try_put(this, MESSAGE_EDGE)) return NO_SPACE;
    }

    if (!this->started || this->order != order) {
        this->order = order;
        this->started == true;
        this->start_put = false;
        this->order_put = false;
    }

    if (!put_start(this)) return 0;
    if (!put_order(this)) return 0;

    for (size_t i = 0; i < count; ++i) {
        if (!put_char(this, data[i])) {
            return i;
        }
    }

    return count;
}

int get_current_order(MessageBuffer* this) {

}

size_t get_contiguous_count(MessageBuffer* this) {

}

ssize_t decapsulate(MessageBuffer* this, char* data, size_t count) {

}

ssize_t skip(MessageBuffer* this, size_t count) {
    
}
