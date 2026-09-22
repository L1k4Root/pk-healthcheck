#ifndef HC_URL_H
#define HC_URL_H

#include <stddef.h>

/* Probe targets are short, local addresses. Fixed buffers keep the code free
 * of dynamic allocation: nothing to leak, nothing to free. */
#define HC_HOST_MAX 256
#define HC_PATH_MAX 1024

enum hc_scheme {
    HC_SCHEME_HTTP, /* http://host[:port][/path]  -> GET request, check status  */
    HC_SCHEME_TCP   /* tcp://host:port            -> connection succeeds or not */
};

struct hc_url {
    enum hc_scheme scheme;
    char host[HC_HOST_MAX]; /* without IPv6 brackets */
    char port[6];           /* "1".."65535" */
    char path[HC_PATH_MAX]; /* starts with '/'; "/" when absent (HTTP only) */
};

/* Parse `input` into `out`. Returns 0 on success, -1 on any invalid input.
 * On failure, `*error` points to a static, human-readable reason. */
int hc_parse_url(const char *input, struct hc_url *out, const char **error);

#endif
