/* Unit tests for the pure parsing code (no network). */
#include <stdio.h>
#include <string.h>

#include "../src/http.h"
#include "../src/url.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

static void test_http_urls(void) {
    struct hc_url u;
    CHECK(hc_parse_url("http://127.0.0.1:8080/readyz", &u, NULL) == 0);
    CHECK(u.scheme == HC_SCHEME_HTTP);
    CHECK(strcmp(u.host, "127.0.0.1") == 0);
    CHECK(strcmp(u.port, "8080") == 0);
    CHECK(strcmp(u.path, "/readyz") == 0);

    CHECK(hc_parse_url("http://localhost", &u, NULL) == 0);
    CHECK(strcmp(u.port, "80") == 0);
    CHECK(strcmp(u.path, "/") == 0);

    CHECK(hc_parse_url("http://svc:3000/health?deep=1", &u, NULL) == 0);
    CHECK(strcmp(u.path, "/health?deep=1") == 0);

    CHECK(hc_parse_url("http://[::1]:9000/livez", &u, NULL) == 0);
    CHECK(strcmp(u.host, "::1") == 0);
    CHECK(strcmp(u.port, "9000") == 0);
}

static void test_tcp_urls(void) {
    struct hc_url u;
    CHECK(hc_parse_url("tcp://postgres:5432", &u, NULL) == 0);
    CHECK(u.scheme == HC_SCHEME_TCP);
    CHECK(strcmp(u.port, "5432") == 0);
    CHECK(hc_parse_url("tcp://db:5432/", &u, NULL) == 0);
}

static void test_invalid_urls(void) {
    struct hc_url u;
    const char *error = NULL;
    const char *invalid[] = {
        "",
        "ftp://host",
        "https://host/health",
        "http://",
        "http://:8080/",
        "http://host:0/",
        "http://host:65536/",
        "http://host:80a/",
        "http://host:123456/",
        "http://[::1/health",
        "http://[::1]x/",
        "http://user@host/",
        "http://host/pa th",
        "http://host/\r\nInjected: 1",
        "tcp://db",
        "tcp://db:5432/path",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        error = NULL;
        int rc = hc_parse_url(invalid[i], &u, &error);
        if (rc != -1 || error == NULL) fprintf(stderr, "  accepted invalid URL: %s\n", invalid[i]);
        CHECK(rc == -1 && error != NULL);
    }
    CHECK(hc_parse_url(NULL, &u, &error) == -1);

    char long_host[400];
    memset(long_host, 'a', sizeof(long_host));
    memcpy(long_host, "http://", 7);
    long_host[sizeof(long_host) - 1] = '\0';
    CHECK(hc_parse_url(long_host, &u, &error) == -1);
}

static void test_request(void) {
    struct hc_url u;
    char buf[512];
    hc_parse_url("http://127.0.0.1:8080/readyz", &u, NULL);
    int n = hc_build_request(&u, buf, sizeof(buf));
    CHECK(n > 0);
    const char *expected = "GET /readyz HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n";
    CHECK(strncmp(buf, expected, strlen(expected)) == 0);
    CHECK(strstr(buf, "Connection: close\r\n\r\n") != NULL);

    hc_parse_url("http://[::1]:9000/", &u, NULL);
    hc_build_request(&u, buf, sizeof(buf));
    CHECK(strstr(buf, "Host: [::1]:9000\r\n") != NULL);

    CHECK(hc_build_request(&u, buf, 10) == -1);
}

static void test_status(void) {
    CHECK(hc_parse_status("HTTP/1.1 200 OK\r\n", 17) == 200);
    CHECK(hc_parse_status("HTTP/1.0 503 Service Unavailable", 32) == 503);
    CHECK(hc_parse_status("HTTP/1.1 204\r\n", 14) == 204);
    CHECK(hc_parse_status("HTTP/1.1 204", 12) == 204);
    /* Partial reads: need more bytes. */
    CHECK(hc_parse_status("HTT", 3) == 0);
    CHECK(hc_parse_status("HTTP/1.1 2", 10) == 0);
    /* Malformed. */
    CHECK(hc_parse_status("SSH-2.0-OpenSSH", 15) == -1);
    CHECK(hc_parse_status("HTTP/2.0 200 OK", 15) == -1);
    CHECK(hc_parse_status("HTTP/1.1 2x0 OK", 15) == -1);
    CHECK(hc_parse_status("HTTP/1.1 099 OK", 15) == -1);
    CHECK(hc_parse_status("HTTP/1.1 600 OK", 15) == -1);
    CHECK(hc_parse_status("HTTP/1.1 2000 OK", 16) == -1);
    CHECK(hc_parse_status("HTTP/1.1_200 OK", 15) == -1);
}

int main(void) {
    test_http_urls();
    test_tcp_urls();
    test_invalid_urls();
    test_request();
    test_status();
    printf("unit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
