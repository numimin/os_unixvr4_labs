#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>

#include <arpa/inet.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s PORT\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* end;
    const in_port_t port = strtol(argv[1], &end, 10);
    if (*end != '\0' || port < 0) {
        fprintf(stderr, "PORT must be a positive integer\n");
        return EXIT_FAILURE;
    }

    const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr;
    struct sockaddr* cast_addr = (struct sockaddr*) &addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sockfd, cast_addr, sizeof(addr))) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char buf[BUFSIZ];
    const char* line = "echo\n";
    while (1) {
        ssize_t count = write(sockfd, line, strlen(line));
        printf("write\n");
        if (count <= 0) {
            perror("write");
            break;
        }
        sleep(1);
        /*count = read(sockfd, buf, BUFSIZ);
        if (count <= 0) {
            perror("read");
            break;
        }
        write(STDOUT_FILENO, buf, count);*/
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
