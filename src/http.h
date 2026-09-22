#ifndef HC_HTTP_H
#define HC_HTTP_H

#include <stddef.h>

#include "url.h"

/* Write a minimal HTTP/1.1 GET request for `url` into `buf`.
 * `Connection: close` lets us read until EOF without parsing Content-Length.
 * Returns the request length, or -1 if it does not fit. */
int hc_build_request(const struct hc_url *url, char *buf, size_t cap);

/* Parse the status code from the start of a response ("HTTP/1.1 204 ...").
 * `len` may be shorter than the full line (partial read). Returns the
 * status (100-599), 0 if more bytes are needed, or -1 if malformed. */
int hc_parse_status(const char *buf, size_t len);

#endif
