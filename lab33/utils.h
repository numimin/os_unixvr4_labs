#ifndef UTILS_H

#define SWAP(T) static void swap_##T (T* lhs, T* rhs) {T tmp = *lhs; *lhs = *rhs; *rhs = tmp;}

SWAP(int)
SWAP(short)

#endif // !UTILS_H