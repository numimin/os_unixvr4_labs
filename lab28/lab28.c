#include <stdlib.h>

int main() {
    #define RAND_MAX 99
    for (size_t i = 0; i < RAND_MAX + 1; ++i) {
        rand();
    }
    return EXIT_SUCCESS;
}