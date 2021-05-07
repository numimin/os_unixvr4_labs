#ifndef MESSAGE_RECEIVER_H
#define MESSAGE_RECEIVER_H

#include "iobuffer.h"

#define MR_NO_ORDER (-1)

typedef struct {
    IOBuffer message;
    char order;
    bool started;
    bool escape;
    bool order_got;
} MessageReceiver;

void free_messagerecv(MessageReceiver* this);
void init_mr(MessageReceiver* this, size_t size);

bool mr_full(const MessageReceiver* this);
bool mr_empty(const MessageReceiver* this);

int get_current_order(MessageReceiver* this);
size_t get_contiguous_count(MessageReceiver* this);
ssize_t decapsulate(MessageReceiver* this, char* data, size_t count);
ssize_t skip(MessageReceiver* this, size_t count);

#endif // !MESSAGE_RECEIVER_H
