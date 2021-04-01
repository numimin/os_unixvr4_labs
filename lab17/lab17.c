#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <ctype.h>

#define MAX_COLUMNS 40

int is_printable(char c);
void backspace(size_t char_num);

typedef struct {
    size_t line[MAX_COLUMNS];
    size_t count;
} words;

int wp_empty(words* this) {
    return this->count == 0;
}

int wp_full(words* this) {
    return this->count == MAX_COLUMNS;
}

void wp_clear(words* this) {
    memset(this, 0, sizeof *this);
}

void wp_add(words* this, char c) {
    if (wp_full(this)) return;
    this->line[this->count++] = c;
}

void wp_add_ar(words* this, const char* s, size_t n) {
    if (n + this->count > MAX_COLUMNS) return;

    for (size_t i = 0; i < n; ++i) {
        this->line[this->count++] = s[i];
    }
}

void wp_del(words* this) {
    if (wp_empty(this)) return;
    --this->count;
}

size_t wp_pop_word(words* this) {
    if (wp_empty(this)) return 0;

    for (int found_word = 0; this->count > 0; --this->count) {
        if (isspace(this->line[this->count - 1])) {
            if (found_word) return this->count;
        } else {
            found_word = 1;
        }
    }
    return 0;
}

size_t wp_get_word(words* this, char word[MAX_COLUMNS]) {
    const size_t word_pos = wp_pop_word(this);

    size_t word_len;
    for (word_len = 0; word_pos + word_len < MAX_COLUMNS && !isspace(this->line[word_pos + word_len]); ++word_len)
        ;

    for (size_t i = 0; i < word_len; ++i) {
        word[i] = this->line[word_pos + i];
    }

    return word_len;
}

typedef struct {
    size_t line_pos;
    words wp;
} tty_cursor;

void tc_clear(tty_cursor* this) {
    this->line_pos = 0;
    wp_clear(&this->wp);
}

void tc_erase(tty_cursor* this) {
    if (this->line_pos) {
        backspace(1);
        wp_del(&this->wp);
        --this->line_pos;
    }
}

void tc_werase(tty_cursor* this) {
    if (wp_empty(&this->wp)) return;

    const size_t word_pos = wp_pop_word(&this->wp);
    backspace(this->line_pos - word_pos);

    this->line_pos = word_pos;
}

void tc_kill(tty_cursor* this) {
    backspace(this->line_pos);
    tc_clear(this);
}

int tc_eol(tty_cursor* this) {
    return this->line_pos && this->line_pos % MAX_COLUMNS == 0;
}

void tc_carry(tty_cursor* this) {
    char word[MAX_COLUMNS];
    size_t word_len = wp_get_word(&this->wp, word);

    if (word_len != MAX_COLUMNS) {
        backspace(word_len);

        write(STDOUT_FILENO, "\n", 1);
        write(STDOUT_FILENO, word, word_len);

        tc_clear(this);
        this->line_pos = word_len;
        wp_add_ar(&this->wp, word, word_len);
        return;
    }

    write(STDOUT_FILENO, "\n", 1);
    tc_clear(this);
}

void tc_nl(tty_cursor* this) {
    write(STDOUT_FILENO, "\n", 1);
    tc_clear(this);
}

void tc_print(tty_cursor* this, char c) {
    if (!is_printable(c)) return;

    if (c == '\n') {
        tc_nl(this);
        return;
    }

    if (tc_eol(this)) {
        if (!isspace(c) && !isspace(this->wp.line[this->line_pos])) {
            tc_carry(this);
        } else {
            tc_nl(this);
            return;
        }
    }

    wp_add(&this->wp, c);
    write(STDOUT_FILENO, &c, 1);
    ++this->line_pos;
}

void tc_bel(tty_cursor* this) {
    write(STDOUT_FILENO, "\a", 1);
}

int main() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("I work only with terminals!\n");
        return EXIT_FAILURE;
    }

    struct termios cur_tc, saved_tc;
    tcgetattr(STDIN_FILENO, &cur_tc);
    memcpy(&saved_tc, &cur_tc, sizeof(saved_tc));

    cur_tc.c_lflag &= ~(ICANON | ISIG | ECHO);

    cur_tc.c_cc[VTIME] = 0;
    cur_tc.c_cc[VMIN] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &cur_tc);

    tty_cursor cursor;
    tc_clear(&cursor);

    for (char c; read(STDIN_FILENO, &c, 1); ) {
        if (!cursor.line_pos && c == cur_tc.c_cc[VEOF]) break;

        if (c == cur_tc.c_cc[VERASE]) {
            tc_erase(&cursor);
            continue;
        } 

        if (c == cur_tc.c_cc[VWERASE]) {
            tc_werase(&cursor);
            continue;
        }

        if (c == cur_tc.c_cc[VKILL]) {
            tc_kill(&cursor);
            continue;
        }

        if (!is_printable(c)) {
            tc_bel(&cursor);
        } else {
            tc_print(&cursor, c);
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tc);
    return EXIT_SUCCESS;
}

void backspace(size_t char_num) {
    for (size_t i = 0; i < char_num; ++i) {
        write(STDOUT_FILENO, "\b \b", sizeof("\b \b"));
    }
}

int is_printable(char c) {
    return isgraph(c) || isspace(c);
}
