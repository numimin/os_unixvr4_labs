#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

typedef struct {
    size_t count;
    size_t capacity;
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

    const int timeout = 5000;
    struct pollfd pfd = {.fd=0, .events=POLLIN};
    for (long long line_num = 1; printf("Type line number: ") && !fflush(stdout); ) {
        const int res = poll(&pfd, 1, timeout);
        if (res == -1) {
            perror("poll");
            close(fd);
            free_table(&lt);
            return EXIT_FAILURE;
        }

        if (res == 0) {
            for (size_t i = 0; i < lt.count; ++i) {
                print_line(&lt, fd, i);
            }
            break;
        }

        if (scanf("%ld", &line_num) != 1) break;

        if (!line_num) break;
        if (line_num < 0) {
            fprintf(stderr, "Line number must be positive\n");
            continue;
        }

        if (line_num > lt.count) {
            fprintf(stderr, "Line number must not exceed line count (%ld)\n",
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
    const size_t line_len = lt->offsets[index + 1] - lt->offsets[index]; 

    lseek(fd, lt->offsets[index], SEEK_SET);
    char* line = malloc(line_len);

    read(fd, line, line_len);
    fflush(stdout);
    write(1, line, line_len);
    if (line[line_len - 1] != '\n') write(1, "\n", sizeof("\n"));

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

        char* old_c = buf;
        for (char* c = memchr(buf, '\n', count); c++; old_c = c, c = memchr(c, '\n', &buf[count] - c) ) {
            line_size += (c - old_c);
            lt_add(this, line_size, offset);
            offset += line_size;
            line_size = 0;
        }
        line_size += (&buf[count] - old_c);
    }

    if (line_size != 0) lt_add(this, line_size, offset);
    return EXIT_SUCCESS;
}

void init_table(line_table* this) {
    this->capacity = 16;
    this->count = 0;
    this->offsets = malloc(sizeof(*this->offsets) * this->capacity);
}

void lt_add(line_table* this, size_t length, off_t offset) {
    lt_reserve(this);
    this->offsets[this->count++] = offset;
    this->offsets[this->count] = offset + length;
}

//Reserves space so that at least one more record can be written 
void lt_reserve(line_table* this) {
    if (this->count + 2 <= this->capacity) return;

    this->capacity *= 1.5;
    this->offsets = realloc(this->offsets, sizeof(*this->offsets) * this->capacity);
}

void free_table(line_table* this) {
    free(this->offsets);
}
 