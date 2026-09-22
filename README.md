# pk-healthcheck

[![ci](https://github.com/L1k4Root/pk-healthcheck/actions/workflows/ci.yml/badge.svg)](https://github.com/L1k4Root/pk-healthcheck/actions/workflows/ci.yml)

A **133 KB static binary**, written in C with no dependencies, that gives any container a Docker
`HEALTHCHECK`, including **distroless and `scratch` images** that have no shell, `curl` or `wget`.

```dockerfile
COPY --from=ghcr.io/l1k4root/pk-healthcheck:1 /healthcheck /healthcheck
HEALTHCHECK --interval=10s --timeout=3s CMD ["/healthcheck", "http://127.0.0.1:8080/readyz"]
```

> **Resumen (ES):** Probe HTTP/TCP de 133 KB escrito en C (sockets POSIX, sin dependencias) para
> `HEALTHCHECK` de Docker. Funciona en imágenes distroless/scratch donde no existe `curl`. Incluye
> timeouts reales con `poll()`, validación estricta de URLs, tests unitarios e integración con
> sanitizers (ASan/UBSan), y el **contrato de endpoints de salud** (`/livez` vs `/readyz`) que siguen
> todos los servicios de platform-kit.

---

## Why

A container that is *running* is not necessarily *working*. Docker only knows the difference if the
image defines a `HEALTHCHECK`, and the usual `CMD curl -f http://localhost/health` does not work in
minimal images:

| Approach | Adds to the image | Works in distroless/scratch |
|---|---|---|
| `curl -f` | ~5 MB (curl + libcurl + TLS) | no |
| `wget -q` (busybox) | ~1 MB | no |
| Healthcheck written in the app's language (e.g. `node healthcheck.js`) | a whole runtime per probe, ~40 MB RSS every 10 s | only if the runtime is there |
| **pk-healthcheck** | **133 KB**, static | **yes** |

[pk-ci-workflows](https://github.com/L1k4Root/pk-ci-workflows) refuses to publish images that never
become healthy, so every platform-kit image needs a probe that works everywhere.

## Usage

```text
healthcheck [-t timeout_ms] [-s min-max] [-v] [URL]

  http://host[:port][/path]   GET; healthy if the status is in range (default 200-299)
  tcp://host:port             healthy if a TCP connection opens (Postgres, Redis, ...)

  -t   total timeout in ms (default 2000; env HEALTHCHECK_TIMEOUT_MS)
  -s   accepted status range, e.g. 200-399
  -v   print the result
  URL  may also come from $HEALTHCHECK_URL

exit status: 0 healthy, 1 unhealthy   (Docker's contract; 2 is reserved and never used)
```

```console
$ healthcheck -v http://127.0.0.1:8080/readyz
healthy: http://127.0.0.1:8080/readyz (HTTP 200, 2ms)
$ healthcheck http://127.0.0.1:8080/readyz
unhealthy: http://127.0.0.1:8080/readyz (HTTP 503, 4ms)
$ healthcheck tcp://127.0.0.1:5432
unhealthy: tcp://127.0.0.1:5432 (Connection refused, 0ms)
```

### Getting the binary

| Method | |
|---|---|
| Docker image | `COPY --from=ghcr.io/l1k4root/pk-healthcheck:1 /healthcheck /healthcheck` (linux/amd64, linux/arm64) |
| GitHub release | `healthcheck-linux-amd64`, `healthcheck-linux-arm64` + `SHA256SUMS` |
| From source | `make` (any POSIX system) or `make static` (musl, e.g. Alpine) |

### Real example: the OpenTelemetry Collector

The Collector image is distroless. [`examples/collector/Dockerfile`](examples/collector/Dockerfile) adds
the probe and a `HEALTHCHECK` against its `:13133` extension, and CI starts the image and waits for
`healthy` on every push.

## How it works (fundamentals)

The whole program is ~400 lines across four files:

| File | Responsibility |
|---|---|
| [`src/url.c`](src/url.c) | Parses `http://` / `tcp://` URLs into fixed-size buffers (**no heap allocation**). Rejects control characters, spaces and `@` so nothing can be smuggled into the request line. IPv6 literals (`[::1]`) are supported. |
| [`src/net.c`](src/net.c) | Resolves names with `getaddrinfo` (IPv4 + IPv6) and uses a **non-blocking `connect` + `poll`** so the connect itself can time out. It reads the final result with `SO_ERROR`. One monotonic deadline covers connect, send and receive. |
| [`src/http.c`](src/http.c) | Writes a minimal `HTTP/1.1` request with `Connection: close` and parses only the status line (`HTTP/1.x NNN`), including partial reads. |
| [`src/main.c`](src/main.c) | CLI parsing with strict integer validation (`strtol` + range + no trailing text) and Docker's exit-code contract. |

Design choices:

- **Read only the status line.** A health endpoint that returns a 5 MB body cannot slow the probe down.
- **A single total deadline** instead of separate connect/read timeouts, so the probe never outlives
  Docker's `--timeout`.
- **No TLS.** The probe runs inside the container against `127.0.0.1`; adding TLS would multiply the
  binary size by ten.
- **`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`**, and every test runs a second time under
  AddressSanitizer and UndefinedBehaviorSanitizer in CI.

## Tests

```bash
make test
```

- `tests/unit.c` has 53 checks for the URL parser, request builder and status-line parser, including
  injection attempts, bad ports, IPv6 and partial reads.
- `tests/integration.sh` runs 25 scenarios of the real binary against
  [`tests/server.py`](tests/server.py), a raw-socket server that misbehaves on purpose: non-HTTP
  responses, early close, a slow endpoint (the timeout must be enforced), a wrong status, a refused
  connection and an unknown host.
- CI runs both with glibc/gcc (plain and with sanitizers), again with musl inside the Docker build, and
  finally the Collector example end to end.

## Health endpoint contract

The probe is half of the story. The other half is what services expose:
**[docs/HEALTH-CONTRACT.md](docs/HEALTH-CONTRACT.md)** covers `/livez` vs `/readyz`, the
`application/health+json` format, timeouts, and why liveness must never check the database.

## Part of platform-kit

One of ten building blocks combined in **pk-checkout-platform**. See
[pk-ci-workflows](https://github.com/L1k4Root/pk-ci-workflows#part-of-platform-kit) for the full list.

## License

[MIT](LICENSE) © Andres (L1k4Root)
