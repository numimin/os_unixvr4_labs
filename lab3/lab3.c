#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

void test_permissions(const char* filename);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }

    test_permissions(argv[1]);
    if (seteuid(getuid()) == -1) {
        fprintf(stderr, "Couldn't set uid to %ld\nReason: %s\n", 
                geteuid(), strerror(errno));
    }
    test_permissions(argv[1]);

    return EXIT_SUCCESS;
}

void test_permissions(const char* filename) {
    printf("UID: %lld\n", getuid());
    printf("Effective UID: %lld\n", geteuid());

    FILE* in = fopen(filename, "r");
    if (!in) {
        fprintf(stderr, "Couldn't open file %s\nReason: %s\n", 
                filename, strerror(errno));
        return;
    }

    fclose(in);
}
