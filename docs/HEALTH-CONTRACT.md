# Health endpoint contract

Every platform-kit service exposes the same two endpoints, whatever its language. The probe in this
repository checks them from inside the container, and orchestrators (Docker Compose, Kubernetes) act on
the result.

## The two questions

| Endpoint | Question | Checks dependencies? | On failure the orchestrator… |
|---|---|---|---|
| `GET /livez` | Is the process alive and not deadlocked? | **No** | restarts the container |
| `GET /readyz` | Can it serve traffic right now? | **Yes** (database, required downstreams) | stops routing traffic to it; no restart |

Mixing the two is a classic outage amplifier. If `/livez` checked the database, a 30-second database
blip would restart **every** replica at the same time. The replicas would then come back cold, all
together, into a database that is still recovering.

## Response format

`Content-Type: application/health+json`. The shape is a simplified form of
[draft-inadarei-api-health-check](https://datatracker.ietf.org/doc/draft-inadarei-api-health-check/).

```http
HTTP/1.1 503 Service Unavailable
Content-Type: application/health+json

{
  "status": "fail",
  "version": "1.4.2",
  "checks": {
    "postgres": { "status": "fail", "duration_ms": 1000, "error": "timeout after 1000ms" },
    "auth-jwks": { "status": "pass", "duration_ms": 3 }
  }
}
```

| Field | Values |
|---|---|
| `status` | `pass` (HTTP 200), `warn` (HTTP 200, degraded but serving), `fail` (HTTP 503) |
| `checks.<name>.status` | same values, per dependency |
| `checks.<name>.duration_ms` | time the check took |
| `checks.<name>.error` | short reason; **no** connection strings, hostnames with credentials, or stack traces |

`/livez` returns `{"status":"pass"}` without `checks`.

## Rules

1. **Cheap and bounded.** Each dependency check has its own timeout (≤ 1 s), and checks run in parallel.
   The Docker `HEALTHCHECK --timeout` must be larger than the probe's `-t` value, which must be larger
   than the slowest check.
2. **No authentication** on `/livez` and `/readyz`. Expose them only on the internal network.
3. **Not traced, not logged at `info`.** Probes run every few seconds. pk-otel ignores these paths by
   default.
4. **Only required dependencies count for readiness.** If a feature can degrade (for example,
   recommendations are down), report `warn` instead of `fail`.
5. **Docker uses `/readyz`.** Docker has a single `HEALTHCHECK`, and `depends_on: condition:
   service_healthy` means "ready to serve". Kubernetes uses both endpoints (`livenessProbe` →
   `/livez`, `readinessProbe` → `/readyz`).

## Dockerfile snippet

```dockerfile
COPY --from=ghcr.io/l1k4root/pk-healthcheck:1 /healthcheck /healthcheck
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 \
  CMD ["/healthcheck", "-t", "2000", "http://127.0.0.1:8080/readyz"]
```

For dependencies without HTTP (Postgres, Redis), a TCP probe proves that the port accepts connections:

```dockerfile
HEALTHCHECK CMD ["/healthcheck", "tcp://127.0.0.1:5432"]
```

A native check such as `pg_isready` is still better when the image has one.
