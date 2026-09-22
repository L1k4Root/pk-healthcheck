/*
 * pk-healthcheck — a tiny, static HTTP/TCP probe for Docker HEALTHCHECK.
 *
 *   HEALTHCHECK CMD ["/healthcheck", "http://127.0.0.1:8080/readyz"]
 *
 * Exit status follows Docker's contract: 0 = healthy, 1 = unhealthy.
 * (Docker reserves 2; this program never returns it.)
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "net.h"
#include "url.h"

#define HEALTHY 0
#define UNHEALTHY 1

#define DEFAULT_TIMEOUT_MS 2000
#define MAX_TIMEOUT_MS 60000

#ifndef HC_VERSION
#define HC_VERSION "dev"
#endif

struct options {
    const char *url;
    long timeout_ms;
    int min_status; /* inclusive */
    int max_status; /* inclusive */
    int verbose;
};

static void usage(FILE *out) {
    fprintf(out,
            "usage: healthcheck [-t timeout_ms] [-s min-max] [-v] [URL]\n"
            "\n"
            "  URL   http://host[:port][/path]  GET, healthy if status in range\n"
            "        tcp://host:port            healthy if the connection opens\n"
            "        (default: $HEALTHCHECK_URL)\n"
            "  -t    total timeout in ms (default %d, env HEALTHCHECK_TIMEOUT_MS)\n"
            "  -s    accepted status range (default 200-299)\n"
            "  -v    print the result to stderr\n"
            "  -V    print version\n"
            "\n"
            "exit status: 0 healthy, 1 unhealthy\n",
            DEFAULT_TIMEOUT_MS);
}

/* Strict integer parsing: the whole string must be a number within range. */
static int parse_long(const char *text, long min, long max, long *out) {
    if (!text || !*text) return -1;
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < min || value > max) return -1;
    *out = value;
    return 0;
}

static int parse_range(const char *text, int *min, int *max) {
    const char *dash = strchr(text, '-');
    if (!dash) return -1;
    char low[4] = {0};
    size_t low_len = (size_t)(dash - text);
    if (low_len == 0 || low_len > 3) return -1;
    memcpy(low, text, low_len);
    long lo, hi;
    if (parse_long(low, 100, 599, &lo) != 0 || parse_long(dash + 1, 100, 599, &hi) != 0) return -1;
    if (lo > hi) return -1;
    *min = (int)lo;
    *max = (int)hi;
    return 0;
}

static int parse_args(int argc, char **argv, struct options *opt) {
    opt->url = getenv("HEALTHCHECK_URL");
    opt->timeout_ms = DEFAULT_TIMEOUT_MS;
    opt->min_status = 200;
    opt->max_status = 299;
    opt->verbose = 0;

    const char *env_timeout = getenv("HEALTHCHECK_TIMEOUT_MS");
    if (env_timeout && parse_long(env_timeout, 1, MAX_TIMEOUT_MS, &opt->timeout_ms) != 0) {
        fprintf(stderr, "healthcheck: invalid HEALTHCHECK_TIMEOUT_MS\n");
        return -1;
    }

    int c;
    while ((c = getopt(argc, argv, "t:s:vVh")) != -1) {
        switch (c) {
        case 't':
            if (parse_long(optarg, 1, MAX_TIMEOUT_MS, &opt->timeout_ms) != 0) {
                fprintf(stderr, "healthcheck: -t expects 1-%d\n", MAX_TIMEOUT_MS);
                return -1;
            }
            break;
        case 's':
            if (parse_range(optarg, &opt->min_status, &opt->max_status) != 0) {
                fprintf(stderr, "healthcheck: -s expects a range like 200-399\n");
                return -1;
            }
            break;
        case 'v':
            opt->verbose = 1;
            break;
        case 'V':
            printf("pk-healthcheck %s\n", HC_VERSION);
            exit(HEALTHY);
        case 'h':
            usage(stdout);
            exit(HEALTHY);
        default:
            usage(stderr);
            return -1;
        }
    }
    if (optind < argc) opt->url = argv[optind++];
    if (optind < argc || !opt->url) {
        usage(stderr);
        return -1;
    }
    return 0;
}

/* Performs the probe. Returns the HTTP status (or 0 for a successful TCP
 * connect) and -1 on failure, with a reason in `*reason`. */
static int probe(const struct hc_url *url, long long deadline, const char **reason) {
    int fd = hc_connect(url->host, url->port, deadline);
    if (fd < 0) {
        *reason = errno == ETIMEDOUT ? "connect timed out" : strerror(errno);
        return -1;
    }
    if (url->scheme == HC_SCHEME_TCP) {
        close(fd);
        return 0;
    }

    char request[HC_PATH_MAX + HC_HOST_MAX + 128];
    int request_len = hc_build_request(url, request, sizeof(request));
    if (request_len < 0 || hc_send_all(fd, request, (size_t)request_len, deadline) != 0) {
        *reason = "failed to send request";
        close(fd);
        return -1;
    }

    /* We only need the status line, so read at most one small buffer's worth. */
    char response[64];
    size_t used = 0;
    int status = 0;
    while (status == 0 && used < sizeof(response)) {
        ssize_t n = hc_recv(fd, response + used, sizeof(response) - used, deadline);
        if (n < 0) {
            *reason = errno == ETIMEDOUT ? "response timed out" : "failed to read response";
            break;
        }
        if (n == 0) {
            *reason = "connection closed before status line";
            break;
        }
        used += (size_t)n;
        status = hc_parse_status(response, used);
        if (status < 0) *reason = "malformed HTTP response";
    }
    close(fd);
    return status > 0 ? status : -1;
}

int main(int argc, char **argv) {
    struct options opt;
    if (parse_args(argc, argv, &opt) != 0) return UNHEALTHY;

    struct hc_url url;
    const char *error = NULL;
    if (hc_parse_url(opt.url, &url, &error) != 0) {
        fprintf(stderr, "healthcheck: %s: %s\n", opt.url, error);
        return UNHEALTHY;
    }

    const long long started = hc_now_ms();
    const char *reason = "unknown error";
    int status = probe(&url, started + opt.timeout_ms, &reason);
    long long elapsed = hc_now_ms() - started;

    int healthy;
    if (status < 0) {
        healthy = 0;
    } else if (url.scheme == HC_SCHEME_TCP) {
        healthy = 1;
    } else {
        healthy = status >= opt.min_status && status <= opt.max_status;
    }

    if (opt.verbose || !healthy) {
        FILE *out = healthy ? stdout : stderr;
        if (status < 0) {
            fprintf(out, "unhealthy: %s (%s, %lldms)\n", opt.url, reason, elapsed);
        } else if (url.scheme == HC_SCHEME_TCP) {
            fprintf(out, "healthy: %s (connected, %lldms)\n", opt.url, elapsed);
        } else {
            fprintf(out, "%s: %s (HTTP %d, %lldms)\n", healthy ? "healthy" : "unhealthy", opt.url,
                    status, elapsed);
        }
    }
    return healthy ? HEALTHY : UNHEALTHY;
}
