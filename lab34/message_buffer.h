#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "iobuffer.h"

#define NO_SPACE (-1)

typedef struct {
    IOBuffer message;
    char order;
    bool started;

    bool escape;
    char escaped_char;

    bool start_put;
    bool order_put;
} MessageBuffer;

void free_messagebuf(MessageBuffer* this);
void init_mb(MessageBuffer* this, size_t size);

ssize_t encapsulate(MessageBuffer* this, const char* data, size_t count, char order);
bool mb_full(const MessageBuffer* this);

int get_current_order(MessageBuffer* this);
size_t get_contiguous_count(MessageBuffer* this);
ssize_t decapsulate(MessageBuffer* this, char* data, size_t count);
ssize_t skip(MessageBuffer* this, size_t count);

#endif // !MESSAGE_BUFFER_H