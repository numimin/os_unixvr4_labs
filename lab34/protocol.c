#include "protocol.h"

size_t message_length(char* data, size_t data_len) {
    size_t length = 0;
    for (size_t i = 0; i < data_len; ++i) {
        length++;
        if (data[i] == MESSAGE_EDGE || data[i] == MESSAGE_ESCAPE) {
            length++;
        }
    }
}

size_t pr_encapsulate(char* data, size_t data_len, char* message, size_t message_len) {
    size_t data_index = 0;
    size_t message_index = 0;
    for (; data_index < data_len && message_index < message_len; data_index++) {
        const char c = data[data_index];

        if (c == MESSAGE_EDGE || c == MESSAGE_ESCAPE) {
            if (message_index + 1 >= message_len) break;
            message[message_index++] = MESSAGE_ESCAPE;
        }

        message[message_index++] = c;
    }

    return data_index;
}
