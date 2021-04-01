#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

struct list {
    char* string;
    struct list* next;
};
typedef struct list list;

list* init_node(const char* string);
void clear_list(list* this);

int main() {
    const size_t max_line_sz = sysconf(_SC_LINE_MAX);
    char* buf = malloc(max_line_sz + 1);
    if (!buf) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    list* head = NULL;
    list* tail = NULL;
    while (fgets(buf, max_line_sz, stdin) != NULL) {
        if (buf[0] == '.') break;

        if (!tail) {
            tail = init_node(buf);
        } else {
            tail->next = init_node(buf);
            tail = tail->next;
        }

        if (!head) head = tail;
    }
    free(buf);

    for (list* l = head; l; l = l->next) {
        printf(l->string);
    }
    
    clear_list(head);
    return EXIT_SUCCESS;
}

list* init_node(const char* string) {
    list* this = calloc(1, sizeof *this);
    this->string = malloc(strlen(string) + 1);
    strcpy(this->string, string);
    return this;
}

void clear_list(list* this) {
    while (this) {
        free(this->string);

        list* next = this->next;
        free(this);
        this = next;
    }
}
