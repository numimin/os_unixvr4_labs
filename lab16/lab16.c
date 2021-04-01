#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

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

    int answer_accepted = 0;
    while (!answer_accepted) {
        printf("Will you accept the lab? [y/n]:");
        fflush(stdout);

        char c;
        read(STDIN_FILENO, &c, 1);
        write(STDIN_FILENO, &c, 1);

        switch (c) {
        case 'y':
        case 'Y':
            printf("\nThank you!\n");
            answer_accepted = 1;
            break;

        case 'n':
        case 'N':
            printf("\nI'll learn theory until the next lesson, I promise.\n");
            answer_accepted = 1;
            break;

        default:
            printf(" You should type y or n\n");
            break;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tc);
    return EXIT_SUCCESS;
}
