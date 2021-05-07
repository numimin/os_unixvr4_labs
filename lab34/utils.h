#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

#define SWAP(T) static void swap_##T (T* lhs, T* rhs) {T tmp = *lhs; *lhs = *rhs; *rhs = tmp;}

#define MAX(T) static T max_##T(T lhs, T rhs) {return lhs < rhs ? rhs : lhs;}
#define MIN(T) static T min_##T(T lhs, T rhs) {return lhs > rhs ? rhs : lhs;}

#define INSTANTIATE_TYPE_TEMPLATES(MACRO, T) \
MACRO(T)

#define INSTANTIATE_BASIC_TYPE_TEMPLATES(MACRO) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, char) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, long) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, int) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, short) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, double) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, float) \
INSTANTIATE_TYPE_TEMPLATES(MACRO, size_t)

INSTANTIATE_BASIC_TYPE_TEMPLATES(SWAP)
INSTANTIATE_BASIC_TYPE_TEMPLATES(MAX)
INSTANTIATE_BASIC_TYPE_TEMPLATES(MIN)

#endif // !UTILS_H