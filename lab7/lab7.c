#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define KB (1 << 10) 
#define MB (KB << 10) 
#define CACHE_SIZE (MB)

typedef struct {
    int fd;

    size_t file_size;
    size_t count;
    size_t capacity;
    off_t* offsets;

    char* cache;
    size_t cache_len;
    size_t cache_idx;
} line_table;

int init_table(line_table* this, int fd);
void lt_reserve(line_table* this);
void lt_add(line_table* this, size_t length, off_t offset);

int get_table(int fd, line_table* this);
void free_table(line_table* this);

int print_line(line_table* lt, size_t index);

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
                if (print_line(&lt, i)) {
                    fprintf(stderr, "Couldn't print line %d\n", index);
                    close(fd);
                    free_table(&lt);
                    return EXIT_FAILURE;
                }
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
            fprintf(stderr, "Line number must not exceed line count (%d)\n",
                    lt.count);
            continue;
        }

        const size_t index = line_num - 1;
        if (print_line(&lt, index)) {
            fprintf(stderr, "Couldn't print line %d\n", index);
            close(fd);
            free_table(&lt);
            return EXIT_FAILURE;
        }
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

long get_page_size() {
    static long page_size = 0;
    if (!page_size) {
        page_size = sysconf(_SC_PAGE_SIZE);
    }
    return page_size;
}

char* get_line(line_table* lt, size_t index) {
    const size_t line_len = lt->offsets[index + 1] - lt->offsets[index]; 
    if (lt->cache &&
        lt->cache_idx <= lt->offsets[index] && 
        lt->cache_idx + lt->cache_len >= lt->offsets[index + 1]
    ) return &lt->cache[lt->offsets[index] - lt->cache_idx]; 
        
    if (lt->cache) {
        int res = munmap(lt->cache, lt->cache_len);

        lt->cache_len = 0;
        lt->cache_idx = 0;
        lt->cache = NULL;

        if (res == -1) {
            perror("munmap");
            return NULL;
        }
    }

    const size_t right_byte_count = lt->file_size - lt->offsets[index];

    const size_t cache_size = (line_len > CACHE_SIZE) ? line_len
                            : (CACHE_SIZE> lt->file_size) ? lt->file_size 
                            : CACHE_SIZE;
    const size_t offset = (right_byte_count < cache_size) ? lt->file_size - cache_size : lt->offsets[index];

    const size_t aligned_offset = (offset / get_page_size()) * get_page_size();
    const size_t aligned_cache_size = cache_size + (offset - aligned_offset);

    lt->cache = mmap(0, aligned_cache_size, PROT_READ, MAP_PRIVATE, lt->fd, aligned_offset);
    if (lt->cache == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    lt->cache_len = aligned_cache_size;
    lt->cache_idx = aligned_offset;
    return &lt->cache[lt->offsets[index] - lt->cache_idx];
}

int print_line(line_table* lt, size_t index) {
    const size_t line_len = lt->offsets[index + 1] - lt->offsets[index]; 

    const char* line = get_line(lt, index);
    if (!line) return EXIT_FAILURE;

    fflush(stdout);
    write(1, line, line_len);
    if (line[line_len - 1] != '\n') printf("\n");
    
    return EXIT_SUCCESS;
}

int get_table(int fd, line_table* this) {
    if (init_table(this, fd)) return EXIT_FAILURE;

    size_t line_size = 0;
    off_t offset = 0;
    char* buf;
    size_t count;
    for (off_t gl_off = 0; gl_off < this->file_size; munmap(buf, count), gl_off += count) {
        count = (this->file_size - gl_off >= CACHE_SIZE) ? CACHE_SIZE : this->file_size - gl_off;
        buf = mmap(0, count, PROT_READ, MAP_PRIVATE, fd, gl_off);

        if (buf == MAP_FAILED) {
            perror("mmap");
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

int init_table(line_table* this, int fd) {
    struct stat statbuf;
    if (fstat(fd, &statbuf) == -1) {
        perror("fstat");
        return EXIT_FAILURE;
    }

    this->capacity = 16;
    this->count = 0;
    this->offsets = malloc(sizeof(*this->offsets) * this->capacity);
    this->cache = NULL;
    this->file_size = statbuf.st_size;
    this->fd = fd;

    return EXIT_SUCCESS;
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
    if (this->cache) munmap(this->cache, this->cache_len);
}
 