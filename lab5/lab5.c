#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

typedef struct {
    size_t count;
    size_t capacity;
    size_t* lengths;
    off_t* offsets;
} line_table;

void init_table(line_table* this);
void lt_reserve(line_table* this);
void lt_add(line_table* this, size_t length, off_t offset);

int get_table(int fd, line_table* this);
void free_table(line_table* this);

void print_line(const line_table* lt, int fd, size_t index);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    const int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Couldn't open file %s\nReason: %s\n",
                argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    line_table lt;
    if (get_table(fd, &lt)) {
        close(fd);
        return EXIT_FAILURE;
    }

    for (long long line_num; printf("Type line number: ") && scanf("%lld", &line_num) == 1;) {
        if (!line_num) break;
        if (line_num < 0) {
            fprintf(stderr, "Line number must be positive\n");
            continue;
        }

        if (line_num > lt.count) {
            fprintf(stderr, "Line number must not exceed line count (%lld)\n",
                    lt.count);
            continue;
        }

        const size_t index = line_num - 1;
        print_line(&lt, fd, index);
    }
    
    close(fd);
    free_table(&lt);

    if (ferror(stdin)) {
        fprintf(stderr, "An I/O error occured: %s\n", 
                strerror(ferror(stdin)) );
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void print_line(const line_table* lt, int fd, size_t index) {
    const size_t line_len = lt->lengths[index];

    lseek(fd, lt->offsets[index], SEEK_SET);
    char* line = malloc(line_len + 1);
    read(fd, line, line_len);
    line[line_len] = '\0';
    printf("%s\n", line);
    free(line);
}

int get_table(int fd, line_table* this) {
    init_table(this);

    char buf[BUFSIZ];
    size_t line_size = 0;
    off_t offset = 0;
    for (ssize_t count; (count = read(fd, buf, BUFSIZ)) != 0;) {
        if (count == -1) {
            perror("read: An I/O error occured");
            free_table(this);
            return EXIT_FAILURE;
        }

        for (size_t i = 0; i < count; ++i) {
            ++line_size;
            if (buf[i] == '\n') {
                //add size without '\n'
                lt_add(this, line_size - 1, offset);
                offset += line_size;
                line_size = 0;
            }
        }
    }

    if (line_size != 0) lt_add(this, line_size, offset);
    return EXIT_SUCCESS;
}

void init_table(line_table* this) {
    this->capacity = 16;
    this->count = 0;
    this->lengths = malloc(sizeof(*this->lengths) * this->capacity);
    this->offsets = malloc(sizeof(*this->offsets) * this->capacity);
}

void lt_add(line_table* this, size_t length, off_t offset) {
    lt_reserve(this);
    this->lengths[this->count] = length;
    this->offsets[this->count++] = offset;
}

//Reserves space so that at least one more record can be written 
void lt_reserve(line_table* this) {
    if (this->count + 1 <= this->capacity) return;

    this->capacity *= 1.5;
    this->lengths = realloc(this->lengths, sizeof(*this->lengths) * this->capacity);
    this->offsets = realloc(this->offsets, sizeof(*this->offsets) * this->capacity);
}

void free_table(line_table* this) {
    free(this->lengths);
    free(this->offsets);
}
