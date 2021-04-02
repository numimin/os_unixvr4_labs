#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <string.h>
#include <errno.h>

typedef struct {
    size_t link_len;
    size_t uname_len;
    size_t gname_len;
    size_t size_len;
} Format;

void print_stat(const char* filename, const struct stat* stat, const Format* format, const char* uname, const char* gname);

size_t max(size_t a, size_t b) {
    return (a > b) ? a : b;
}

size_t number_size(size_t num) {
    static char buf[64];
    sprintf(buf, "%d", num);
    return strlen(buf);
}

char* num_to_str(size_t num) {
    char* buf = malloc(number_size(num) + 1);
    sprintf(buf, "%d", num);
    return buf;
}

char* str_copy(char* s) {
    char* buf = malloc(strlen(s) + 1);
    return strcpy(buf, s);
}

char* get_uname(const struct stat* stat) {
    const struct passwd* pwd_uid = getpwuid(stat->st_uid);
    if (pwd_uid != NULL) {
        return str_copy(pwd_uid->pw_name);
    }

    return num_to_str(stat->st_uid);
}

char* get_gname(const struct stat* stat) {
    const struct group* grp_gid = getgrgid(stat->st_gid);
    if (grp_gid != NULL) {
        return str_copy(grp_gid->gr_name);
    }

    return num_to_str(stat->st_gid);
}

char* default_args[] = {"."};

int main(int argc, char* argv[]) {
    char** files = (argc == 1) ? default_args : &argv[1];
    const size_t file_count = (argc == 1) ? 1 : argc - 1;

    Format format = {};

    struct stat* stats = calloc(file_count, sizeof *stats);
    char** unames = calloc(file_count, sizeof *unames);
    char** gnames = calloc(file_count, sizeof *gnames);
    int* errors = calloc(file_count, sizeof *errors);

    for (size_t i = 0; i < file_count; ++i) {
        if (lstat(files[i], &stats[i]) == -1) {
            errors[i] = errno;
            continue;
        }

        unames[i] = get_uname(&stats[i]);
        gnames[i] = get_uname(&stats[i]);

        format.uname_len = max(strlen(unames[i]), format.uname_len); 
        format.gname_len = max(strlen(gnames[i]), format.gname_len); 
        const size_t size_len = S_ISREG(stats[i].st_mode) ? number_size(stats[i].st_size) : 0;
        format.size_len = max(size_len, format.size_len);
        format.link_len = max(number_size(stats[i].st_nlink), format.link_len);
    }

    for (size_t i = 0; i < file_count; ++i) {
        if (errors[i]) {
            fprintf(stderr, "%s: couldn't read status of %s: %s\n", argv[0], files[i], strerror(errors[i]));
            continue;
        }

        print_stat(files[i], &stats[i], &format, unames[i], gnames[i]);
        free(unames[i]);
        free(gnames[i]);
    }

    free(unames);
    free(gnames);
    free(stats);
    free(errors);

    return EXIT_SUCCESS;
}

void print_mode(mode_t st_mode) {
    const char mode = S_ISDIR(st_mode) ? 'd' :
                      S_ISREG(st_mode) ? '-' : '?';

    printf("%c", mode);

    const static char has_access[3] = {'r', 'w', 'x'};
    const static char* no_access[3] = {"√", "√", "-"}; 

    const static mode_t access_bits[3][3] = {
        {S_IRUSR, S_IWUSR, S_IXUSR},
        {S_IRGRP, S_IWGRP, S_IXGRP}, 
        {S_IROTH, S_IWOTH, S_IXOTH}, 
    };

    for (size_t own = 0; own < 3; ++own) {
        for (size_t type = 0; type < 3; ++type) {
            if (access_bits[own][type] & st_mode) {
                printf("%c", has_access[type]);
            } else {
                printf("%s", no_access[type]);
            }
        }
    }

    printf(" ");
}

void print_time(const time_t* timer) {
    const char* time_str = ctime(timer);
    const size_t len_without_nl = strchr(time_str, '\n') - time_str;
    printf("%.*s ", len_without_nl, time_str);
}

void print_stat(const char* filename, const struct stat* stat, const Format* format, const char* uname, const char* gname) {
    print_mode(stat->st_mode);

    printf("%*d ", format->link_len, stat->st_nlink);

    printf("%*s %*s ", format->uname_len, uname, format->gname_len, gname);

    if (S_ISREG(stat->st_mode)) {
        printf("%*d ", format->size_len, stat->st_size);
    } else {
        printf("%*s ", format->size_len, "");
    }

    print_time(&stat->st_mtim.tv_sec);
    printf("%s\n", basename(filename));
}
