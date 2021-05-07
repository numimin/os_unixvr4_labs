#include "transport_protocol_buffer.h"

#include "utils.h"
#include "protocol.h"

void tpb_free(TPBuffer* this) {
    cb_free(&this->buffer);
}

void tpb_init(TPBuffer* this, size_t size) {
    cb_init(&this->buffer, size);
    this->order = TPB_NO_ORDER;
}

bool tpb_full(const TPBuffer* this) {
    return cb_full(&this->buffer);
}

bool tpb_empty(const TPBuffer* this) {
    return cb_empty(&this->buffer);
}

void tpb_put_order(TPBuffer* this, int order) {
    if (this->order != TPB_NO_ORDER) {
        cb_putc(&this->buffer, MESSAGE_EDGE);
    }

    cb_putc(&this->buffer, MESSAGE_EDGE);
    cb_putc(&this->buffer, CLIENT_CODE_SHIFT + (char) order);
    this->order = order;
}

bool tpb_contol_message(TPBuffer* this, char op, int order) {
    if (this->order != CONTROL_ORDER) {
        const size_t control_length = this->order == TPB_NO_ORDER ? 2 : 3;
        if (EVENT_MESSAGE_LENGTH + control_length > cb_free_space(&this->buffer)) {
            return false;
        }

        tpb_put_order(this, CONTROL_ORDER);
    }

    cb_putc(&this->buffer, op);
    cb_putc(&this->buffer, CLIENT_CODE_SHIFT + order);

    return true;
}

size_t tpb_encapsulate(TPBuffer* this, char* data, size_t data_length, int order) {
    if (order > TPB_MAX_ORDER || order < 0) return 0;

    if (order != this->order) {
        const size_t control_length = this->order == TPB_NO_ORDER ? 2 : 3;
        if (message_length(data, 1) + control_length > cb_free_space(&this->buffer)) {
            return 0;
        }

        tpb_put_order(this, order);
    }

    size_t encapsed = 0;
    for (size_t i = 0; i < 2; ++i) {
        const size_t msg_length = message_length(&data[encapsed], data_length);
        const size_t contiguous_space = cb_free_contiguous_space(&this->buffer);

        size_t count = pr_encapsulate(data, data_length, 
            cb_data_end(&this->buffer), min_size_t(msg_length, contiguous_space));

        cb_skip_right(&this->buffer, count);
        encapsed += count;

        if (count < contiguous_space) return encapsed;
    }

    return encapsed;
}

ssize_t tpb_send(TPBuffer* this, int fd) {
    return cb_send(&this->buffer, fd);
}
