#include "message_receiver.h"

#include <string.h>

#define MESSAGE_EDGE 0x7E
#define MESSAGE_ESCAPE 0b01111101

void free_messagerecv(MessageReceiver* this) {
    free_iobuf(&this->message);
}

void init_mr(MessageReceiver* this, size_t size) {
    memset(this, 0, sizeof(*this));
    init_iobuf(&this->message, size);
}

bool mr_full(const MessageReceiver* this) {
    return iob_full(&this->message);
}

bool mr_empty(const MessageReceiver* this) {
    return iob_empty(&this->message);
}

#include <stdio.h>

bool try_skip_start(MessageReceiver* this) {
    if (this->started) return true;
    if (mr_empty(this)) return false;

    iob_shift(&this->message, 1);
    this->started = true;
    return true;
}

#define MESSAGE_BOUNDARY (-2)

int try_read_char(MessageReceiver* this) {
    if (mr_empty(this)) return END_OF_BUFFER;

    IOBuffer* msg = &this->message;

    const char c = iob_getc(msg);
    if (this->escape) {
        this->escape = false;
        return c;
    }

    if (c == MESSAGE_ESCAPE) {
        this->escape = true;
        if (mr_empty(this)) return END_OF_BUFFER;

        this->escape = false;
        return iob_getc(msg);
    }

    if (c == MESSAGE_EDGE) {
        if (this->started) {
            this->started = false;
            this->order_got = false;
        }

        return MESSAGE_BOUNDARY;
    }

    return c;
}

int try_parse_order(MessageReceiver* this) {
    if (this->order_got) return this->order;

    if (!try_skip_start(this)) return MR_NO_ORDER;

    const int order = try_read_char(this);
    if (order == MESSAGE_BOUNDARY || order == END_OF_BUFFER) {
        return MR_NO_ORDER;
    }

    this->order = order;
    this->order_got = true;
    return order;
}

int get_current_order(MessageReceiver* this) {
   return try_parse_order(this);
}

size_t get_contiguous_count(MessageReceiver* this) {
    if (try_parse_order(this) == MR_NO_ORDER) return 0;
    char* buffer = this->message.buf;
    const size_t buf_len = this->message.count;

    bool escaped = this->escape;
    size_t count = 0;
    for (size_t i = 0; i < buf_len; ++i) {
        if (!escaped && buffer[i] == MESSAGE_ESCAPE) {
            escaped = true;
            continue;
        }

        escaped = false;
        count++;
    }

    return count;
}

ssize_t decapsulate(MessageReceiver* this, char* data, size_t count) {
    if (try_parse_order(this) == MR_NO_ORDER) return -1;

    size_t read_count = 0;
    int c;
    while (read_count < count && (c = try_read_char(this)) >= 0) {
        data[read_count++] = c;
    }

    return read_count;
}

ssize_t skip(MessageReceiver* this, size_t count) {
    if (try_parse_order(this) == MR_NO_ORDER) return -1;

    size_t skipped = 0;
    while (skipped < count && try_read_char(this) >= 0)
        ;

    return skipped;
}
