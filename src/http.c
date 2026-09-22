#include "http.h"

#include <stdio.h>
#include <string.h>

int hc_build_request(const struct hc_url *url, char *buf, size_t cap) {
    /* IPv6 literals need their brackets back in the Host header. */
    const int ipv6 = strchr(url->host, ':') != NULL;
    int n = snprintf(buf, cap,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s%s%s:%s\r\n"
                     "User-Agent: pk-healthcheck\r\n"
                     "Accept: */*\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     url->path, ipv6 ? "[" : "", url->host, ipv6 ? "]" : "", url->port);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

int hc_parse_status(const char *buf, size_t len) {
    /* "HTTP/1.x SSS" is 12 bytes; wait until we have them. */
    static const char prefix[] = "HTTP/1.";
    const size_t prefix_len = sizeof(prefix) - 1;

    size_t check = len < prefix_len ? len : prefix_len;
    if (memcmp(buf, prefix, check) != 0) return -1;
    if (len < 12) return 0;

    if (buf[7] != '0' && buf[7] != '1') return -1;
    if (buf[8] != ' ') return -1;
    int status = 0;
    for (int i = 9; i < 12; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        status = status * 10 + (buf[i] - '0');
    }
    if (status < 100 || status > 599) return -1;
    /* The code must be followed by a space or the end of the line. */
    if (len > 12 && buf[12] != ' ' && buf[12] != '\r') return -1;
    return status;
}
