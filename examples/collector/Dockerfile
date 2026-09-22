# Real-world example: the OpenTelemetry Collector image is distroless (no
# shell, curl or wget), so it cannot declare a HEALTHCHECK on its own.
# Adding the ~130 KB static probe fixes that.
#
# Build from the repository root:
#   docker build -f examples/collector/Dockerfile -t collector-with-healthcheck .
#
# In your own images, replace the build stage with:
#   COPY --from=ghcr.io/l1k4root/pk-healthcheck:1 /healthcheck /healthcheck

FROM alpine:3.24 AS healthcheck
RUN apk add --no-cache build-base
WORKDIR /src
COPY Makefile ./
COPY src ./src
RUN make static

FROM otel/opentelemetry-collector-contrib:0.161.0
COPY --from=healthcheck /src/build/healthcheck /healthcheck
# The health_check extension listens on :13133 in the default configuration.
HEALTHCHECK --interval=5s --timeout=3s --start-period=5s --retries=3 \
  CMD ["/healthcheck", "http://127.0.0.1:13133/"]
