#define _POSIX_C_SOURCE 200809L

#include "net.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* macOS: SIGPIPE is disabled per socket below instead */
#endif

long long hc_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Wait until `fd` is ready for `events` or the deadline passes.
 * Returns 1 when ready, 0 on timeout, -1 on error. */
static int wait_for(int fd, short events, long long deadline_ms) {
    for (;;) {
        long long remaining = deadline_ms - hc_now_ms();
        if (remaining <= 0) {
            errno = ETIMEDOUT;
            return 0;
        }
        struct pollfd pfd = {.fd = fd, .events = events, .revents = 0};
        int rc = poll(&pfd, 1, (int)remaining);
        if (rc < 0 && errno == EINTR) continue;
        if (rc == 0) {
            errno = ETIMEDOUT;
            return 0;
        }
        return rc < 0 ? -1 : 1;
    }
}

/* Non-blocking connect: start it, then poll for writability and read the
 * final result with SO_ERROR. This is how a connect gets a timeout. */
static int connect_one(const struct addrinfo *ai, long long deadline_ms) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;

#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) goto fail;

    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) return fd;
    if (errno != EINPROGRESS) goto fail;

    if (wait_for(fd, POLLOUT, deadline_ms) != 1) goto fail;

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) goto fail;
    if (so_error != 0) {
        errno = so_error;
        goto fail;
    }
    return fd;

fail:;
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

int hc_connect(const char *host, const char *port, long long deadline_ms) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results = NULL;
    int gai = getaddrinfo(host, port, &hints, &results);
    if (gai != 0) {
        errno = EHOSTUNREACH;
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = results; ai && fd < 0; ai = ai->ai_next) {
        fd = connect_one(ai, deadline_ms);
    }
    freeaddrinfo(results);
    return fd;
}

int hc_send_all(int fd, const char *buf, size_t len, long long deadline_ms) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (wait_for(fd, POLLOUT, deadline_ms) != 1) return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

ssize_t hc_recv(int fd, char *buf, size_t cap, long long deadline_ms) {
    for (;;) {
        ssize_t n = recv(fd, buf, cap, 0);
        if (n >= 0) return n;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
        if (wait_for(fd, POLLIN, deadline_ms) != 1) return -1;
    }
}
