#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define MESSAGE_EDGE 0x7E
#define MESSAGE_ESCAPE 0b01111101

size_t message_length(char* data, size_t data_len);
size_t pr_encapsulate(char* data, size_t data_len, char* message, size_t message_len);

#endif //! PROTOCOL_H