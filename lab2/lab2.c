#define _XOPEN_SOURCE

#include <sys/types.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    putenv("TZ=<California-local-time>+08");
    
    time_t now = time(NULL);
    struct tm* sp = localtime(&now);
    printf("California time: %d/%d/%02d %d:%02d %s\n",
           sp->tm_mon + 1, 
           sp->tm_mday,
           sp->tm_year + 1900, sp->tm_hour,
           sp->tm_min, tzname[sp->tm_isdst]);

    putenv("TZ=:America/Los_Angeles");
    sp = localtime(&now);
    printf("California time: %d/%d/%02d %d:%02d %s\n",
           sp->tm_mon + 1, 
           sp->tm_mday,
           sp->tm_year + 1900, sp->tm_hour,
           sp->tm_min, tzname[sp->tm_isdst]);

    return EXIT_SUCCESS;
}