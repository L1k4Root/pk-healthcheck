#ifndef HC_NET_H
#define HC_NET_H

#include <stddef.h>
#include <sys/types.h>

/* Milliseconds on a monotonic clock (immune to wall-clock changes). */
long long hc_now_ms(void);

/* Connect to host:port before `deadline_ms` (a hc_now_ms() value).
 * Tries every address getaddrinfo returns (IPv4 and IPv6).
 * Returns a connected, non-blocking socket, or -1 (errno is set). */
int hc_connect(const char *host, const char *port, long long deadline_ms);

/* Send all `len` bytes before the deadline. Returns 0 or -1. */
int hc_send_all(int fd, const char *buf, size_t len, long long deadline_ms);

/* Receive up to `cap` bytes, waiting until the deadline for data.
 * Returns bytes read, 0 on EOF, or -1 on error/timeout. */
ssize_t hc_recv(int fd, char *buf, size_t cap, long long deadline_ms);

#endif
