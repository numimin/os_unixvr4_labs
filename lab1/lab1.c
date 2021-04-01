#define _XOPEN_SOURCE //getopt
#define _GNU_SOURCE //environ

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <errno.h>

typedef struct {
    char c;
    char* arg;
} option;

void clear_options(option* array, size_t count);
int peek_options(int argc, char** argv, size_t* opt_count);
void execute_options(option* options, size_t count);
void execute_option(option opt);

#define OPTSTRING ":ispuU:cC:dvV:"

int main(int argc, char* argv[]) {
    size_t opt_count;
    if (peek_options(argc, argv, &opt_count)) {
        fprintf(stderr, "Usage: %s [-ispucdv] [-Unew_ulimit] [-Csize] [-Vname=value]\n", argv[0]);
        return EXIT_FAILURE;
    }

    option* options = calloc(opt_count, sizeof *options);
    optind = 1;
    for (size_t i = 0; (options[i].c = getopt(argc, argv, OPTSTRING)) != -1; ++i) {
        if (optarg != NULL) {
            options[i].arg = malloc(strlen(optarg) + 1);
            strcpy(options[i].arg, optarg);
        }
    }

    execute_options(options, opt_count);
    clear_options(options, opt_count);
    return EXIT_SUCCESS;
}

void execute_options(option* options, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        execute_option(options[count - 1 - i]);
    }
}

void execute_option(option opt) {
    switch (opt.c) {
        case 'i': {
            printf("UID: %d\n", getuid());
            printf("Effective UID: %d\n", geteuid());
            printf("GID: %d\n", getgid());
            printf("Effective GID: %d\n", getegid());
            break;
        }

        case 's': {
            if (setpgid(0, 0) == -1) {
                perror("Failed to make this process a group leader.\nReason");
            }
            break;
        }

        case 'p': {
            printf("PID: %d\n", getpid());
            printf("PPID: %d\n", getppid());
            printf("GID: %d\n", getpgrp());
            break;
        }

        case 'u': {
            struct rlimit rlp;
            if (getrlimit(RLIMIT_FSIZE, &rlp) == -1) {
                perror("Failed to get file size limit\nReason");
            }

            if (rlp.rlim_cur == RLIM_INFINITY) {
                printf("File size is unlimited\n");
            } else {
                printf("File size limit: %d\n", rlp.rlim_cur);
            }
            break;
        }

        case 'U': {
            const long new_size = atol(opt.arg);

            struct rlimit rlp;
            if (getrlimit(RLIMIT_FSIZE, &rlp) == -1) {
                perror("Failed to access file size limit\nReason");
            };

            rlp.rlim_cur = new_size;
            if (setrlimit(RLIMIT_FSIZE, &rlp) == -1) {
                
                fprintf(stderr, "Failed to set file size limit to %ld\nReason: %s\n", 
                        new_size, strerror(errno));
            }
            break;
        }

        case 'c': {
            struct rlimit rlp;
            if (getrlimit(RLIMIT_CORE, &rlp) == -1) {
                perror("Failed to get core limit\nReason");
            }

            if (rlp.rlim_cur == RLIM_INFINITY) {
                printf("Core is unlimited\n");
            } else {
                printf("Core limit: %d\n", rlp.rlim_cur);
            }
            break;
        }

        case 'C': {
            const long new_size = atol(opt.arg);

            struct rlimit rlp;
            if (getrlimit(RLIMIT_CORE, &rlp) == -1) {
                perror("Failed to access core limit\nReason");
            };

            rlp.rlim_cur = new_size;
            if (setrlimit(RLIMIT_CORE, &rlp) == -1) {
                fprintf(stderr, "Failed to set core limit to %ld\nReason: %s\n", 
                        new_size, strerror(errno));
            }
            break;
        }

        case 'd': {
            char* cwd = get_current_dir_name();
            printf("Working directory: %s\n", cwd);
            free(cwd);
            break;
        }

        case 'v': {
            printf("%s\n", 
                (*environ) ? "Environmental variables:"
                           : "There are no environmental variables"
            );

            for (char** var = environ; *var; ++var) {
                printf("%s\n", *var);
            }
            break;
        }

        case 'V': {
            if (putenv(opt.arg) != 0) {
                fprintf(stderr, "Failed to put environment variable %s\nReason: %s\n", 
                        opt.arg, strerror(errno));
            }
            break;
        }

        default: break; //Impossible if the program is correct
    }
}

void clear_options(option* array, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        free(array[i].arg);
    }
    free(array);
}

int peek_options(int argc, char** argv, size_t* opt_count) {
    *opt_count = 0;
    int has_errors = 0;

    optind = 1;
    for (int opt; (opt = getopt(argc, argv, OPTSTRING)) != -1; ++(*opt_count)) {
        switch (opt) {
        case '?': {
            has_errors = 1;
            fprintf(stderr, "Unknown option -%c\n", (char) optopt);
            break;
        }

        case ':': {
            has_errors = 1;
            fprintf(stderr, "No argument provided for -%c\n", (char) optopt);
            break;
        }

        case 'U':
        case 'C': {
            char* endptr = NULL;
            errno = 0;
            long res = strtol(optarg, &endptr, 10);
            if (*endptr != '\0' || res < 0) {
                has_errors = 1;
                fprintf(stderr, "Argument of -%c must be unsigned integer\n", opt);
                break;
            }
            
            if (errno) {
                has_errors = 1;
                fprintf(stderr, "Argument of %-c is too big\n", opt);
            }
            break;
        }

        case 'V': {
            char* eq;
            if ((eq = strpbrk(optarg, "=")) == NULL || 
                eq == optarg || //no name
                strpbrk(&eq[1], "=") != NULL) {

                has_errors = 1;
                fprintf(stderr, "Argument of -%c must be of NAME=VALUE type\n", opt);
            }
            break;
        }
        
        default:
            break;
        }
    }

    const int invalid_option_count = (optind < argc);
    return has_errors || invalid_option_count;
}
