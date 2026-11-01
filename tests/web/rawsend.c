/* rawsend — send stdin verbatim to 127.0.0.1:PORT and print the reply.
 * Usage: rawsend PORT [keep]   ("keep": don't half-close after sending,
 * so the server must time the request out itself). */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)atoi(argv[1]));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) {
        perror("connect");
        return 1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
        if (write(fd, buf, n) != (ssize_t)n)
            break;
    if (argc < 3 || strcmp(argv[2], "keep"))
        shutdown(fd, SHUT_WR);
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 15000) <= 0)
            break;
        ssize_t r = read(fd, buf, sizeof buf);
        if (r <= 0)
            break;
        fwrite(buf, 1, (size_t)r, stdout);
    }
    return 0;
}
