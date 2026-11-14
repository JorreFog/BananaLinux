/*
 * banana-hwc: a command-line client for banana-hwd, for adb shell and the tests.
 *
 *   banana-hwc [-s socket] <command words...>         one request, its reply printed; exit 0 on OK, 1 on ERR
 *   banana-hwc [-s socket] -e <count> [-t ms]         subscribe and print <count> events (or until -t ms pass)
 *
 *   adb shell banana-hwc STATS
 *   adb shell banana-hwc FAN MODE aggressive
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int connect_to(const char *path)
{
    struct sockaddr_un a;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) {
        close(fd);
        return -1;
    }
    return fd;
}

/* one line from fd into buf, waiting at most ms (negative: for ever); 0 on a line, -1 on timeout or EOF */
static int read_line(int fd, char *buf, size_t n, int ms)
{
    size_t len = 0;
    while (len < n - 1) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        char ch;
        int r = poll(&p, 1, ms);
        if (r <= 0)
            return -1;
        if (read(fd, &ch, 1) != 1)
            return -1;
        if (ch == '\n')
            break;
        buf[len++] = ch;
    }
    buf[len] = 0;
    return 0;
}

int main(int argc, char **argv)
{
    const char *sock = getenv("BANANA_HWD_SOCKET");
    char line[4200], reply[4200];
    int opt, events = 0, ms = -1, fd;
    size_t len = 0;
    if (!sock || !*sock)
        sock = "/dev/socket/banana-hwd";
    while ((opt = getopt(argc, argv, "+s:e:t:")) != -1) {
        if (opt == 's') sock = optarg;
        else if (opt == 'e') events = atoi(optarg);
        else if (opt == 't') ms = atoi(optarg);
        else return 2;
    }
    if (!events && optind >= argc) {
        fprintf(stderr, "usage: banana-hwc [-s socket] <command...> | -e <count> [-t ms]\n");
        return 2;
    }
    if ((fd = connect_to(sock)) < 0) {
        fprintf(stderr, "banana-hwc: %s: %s\n", sock, strerror(errno));
        return 3;
    }
    if (events) {
        if (write(fd, "SUBSCRIBE\n", 10) != 10 || read_line(fd, reply, sizeof reply, 5000) || strcmp(reply, "OK"))
            return 1;
        puts("subscribed");
        fflush(stdout);
        for (int i = 0; i < events; i++) {
            if (read_line(fd, reply, sizeof reply, ms))
                return 1;
            puts(reply);
            fflush(stdout);
        }
        return 0;
    }
    for (int i = optind; i < argc; i++)
        len += (size_t)snprintf(line + len, sizeof line - len, "%s%s", i > optind ? " " : "", argv[i]);
    if (len >= sizeof line - 1)
        return 2;
    line[len++] = '\n';
    if (write(fd, line, len) != (ssize_t)len || read_line(fd, reply, sizeof reply, 10000))
        return 1;
    puts(reply);
    return strncmp(reply, "OK", 2) ? 1 : 0;
}
