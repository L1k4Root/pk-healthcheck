#include "url.h"

#include <stdlib.h>
#include <string.h>

static int fail(const char **error, const char *reason) {
    if (error) *error = reason;
    return -1;
}

/* Copy [start, start+len) into dst (capacity cap) as a C string. */
static int copy_span(char *dst, size_t cap, const char *start, size_t len) {
    if (len >= cap) return -1;
    memcpy(dst, start, len);
    dst[len] = '\0';
    return 0;
}

static int valid_port(const char *start, size_t len) {
    if (len == 0 || len > 5) return 0;
    long value = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] < '0' || start[i] > '9') return 0;
        value = value * 10 + (start[i] - '0');
    }
    return value >= 1 && value <= 65535;
}

int hc_parse_url(const char *input, struct hc_url *out, const char **error) {
    if (!input || !out) return fail(error, "missing URL");
    memset(out, 0, sizeof(*out));

    const char *rest;
    if (strncmp(input, "http://", 7) == 0) {
        out->scheme = HC_SCHEME_HTTP;
        rest = input + 7;
    } else if (strncmp(input, "tcp://", 6) == 0) {
        out->scheme = HC_SCHEME_TCP;
        rest = input + 6;
    } else if (strncmp(input, "https://", 8) == 0) {
        return fail(error, "https is not supported: probe the plain-HTTP port on localhost");
    } else {
        return fail(error, "URL must start with http:// or tcp://");
    }

    /* Authority ends at the first '/' (or the end of the string). */
    const char *slash = strchr(rest, '/');
    const char *authority_end = slash ? slash : rest + strlen(rest);

    /* Host: "[v6]" or everything up to the last ':' of the authority. */
    const char *host_start = rest;
    const char *host_end;
    const char *after_host;
    if (*rest == '[') {
        const char *close = memchr(rest, ']', (size_t)(authority_end - rest));
        if (!close) return fail(error, "unterminated IPv6 address");
        host_start = rest + 1;
        host_end = close;
        after_host = close + 1;
    } else {
        const char *colon = memchr(rest, ':', (size_t)(authority_end - rest));
        host_end = colon ? colon : authority_end;
        after_host = host_end;
    }
    if (host_end == host_start) return fail(error, "missing host");
    if (copy_span(out->host, sizeof(out->host), host_start, (size_t)(host_end - host_start)) != 0)
        return fail(error, "host is too long");
    for (const char *c = out->host; *c; c++) {
        /* Reject anything that could smuggle bytes into the request line. */
        if (*c <= ' ' || *c == '@' || *c == '\x7f') return fail(error, "invalid character in host");
    }

    /* Port. */
    if (after_host < authority_end) {
        if (*after_host != ':') return fail(error, "unexpected text after host");
        const char *port_start = after_host + 1;
        size_t port_len = (size_t)(authority_end - port_start);
        if (!valid_port(port_start, port_len)) return fail(error, "port must be 1-65535");
        copy_span(out->port, sizeof(out->port), port_start, port_len);
    } else if (out->scheme == HC_SCHEME_HTTP) {
        strcpy(out->port, "80");
    } else {
        return fail(error, "tcp:// URLs need an explicit port");
    }

    /* Path (HTTP only). */
    if (out->scheme == HC_SCHEME_HTTP) {
        const char *path = slash ? slash : "/";
        if (copy_span(out->path, sizeof(out->path), path, strlen(path)) != 0)
            return fail(error, "path is too long");
        for (const char *c = out->path; *c; c++) {
            if (*c <= ' ' || *c == '\x7f') return fail(error, "invalid character in path");
        }
    } else if (slash && slash[1] != '\0') {
        return fail(error, "tcp:// URLs cannot have a path");
    }
    return 0;
}
