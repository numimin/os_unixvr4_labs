#ifndef TRANSPORT_PROTOCOL_BUFFER_H
#define TRANSPORT_PROTOCOL_BUFFER_H

#include "cyclic_buffer.h"

#define TPB_NO_ORDER (-1)
#define TPB_MAX_ORDER 255

#ifdef DEBUG

#define CLIENT_ADD 'A'
#define CLIENT_REMOVE 'R'

#else 

#define CLIENT_ADD 1
#define CLIENT_REMOVE 2

#endif // DEBUG

#ifdef DEBUG
#define CLIENT_CODE_SHIFT 'a'
#else 
#define CLIENT_CODE_SHIFT 0
#endif // DEBUG

#define EVENT_MESSAGE_LENGTH 2

#define CONTROL_ORDER 255

typedef struct {
    CyclicBuffer buffer;
    int order;
} TPBuffer;

void tpb_free(TPBuffer* this);
void tpb_init(TPBuffer* this, size_t size);

bool tpb_full(const TPBuffer* this);
bool tpb_empty(const TPBuffer* this);

size_t tpb_encapsulate(TPBuffer* this, char* data, size_t data_length, int order);
bool tpb_contol_message(TPBuffer* this, char op, int order);
ssize_t tpb_send(TPBuffer* this, int fd);

#endif // !TRANSPORT_PROTOCOL_BUFFER_H