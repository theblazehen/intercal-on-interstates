/* rawsend — send stdin verbatim to 127.0.0.1:PORT and print the reply.
 * Usage: rawsend PORT [keep]
 * Stdin is forwarded as it arrives (so a pipeline with sleeps sends
 * requests over time). At stdin EOF the socket is half-closed, unless
 * "keep" is given, so the server must end the exchange itself. Exits when
 * the server closes, or after 15 s of silence. */
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
    int keep = argc >= 3 && !strcmp(argv[2], "keep");
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
    int in_open = 1;
    for (;;) {
        struct pollfd p[2] = {{fd, POLLIN, 0}, {0, POLLIN, 0}};
        if (poll(p, in_open ? 2 : 1, 15000) <= 0)
            break;
        if (p[0].revents) {
            ssize_t r = read(fd, buf, sizeof buf);
            if (r <= 0)
                break;
            fwrite(buf, 1, (size_t)r, stdout);
            fflush(stdout);
        }
        if (in_open && p[1].revents) {
            ssize_t n = read(0, buf, sizeof buf);
            if (n <= 0) {
                in_open = 0;
                if (!keep)
                    shutdown(fd, SHUT_WR);
            } else if (write(fd, buf, (size_t)n) != n) {
                in_open = 0;
            }
        }
    }
    return 0;
}
