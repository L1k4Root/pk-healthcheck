# Builds a fully static, stripped binary with musl and ships it in an empty
# image. Other images copy it in with one line:
#
#   COPY --from=ghcr.io/l1k4root/pk-healthcheck:1 /healthcheck /healthcheck
#   HEALTHCHECK CMD ["/healthcheck", "http://127.0.0.1:8080/readyz"]

FROM alpine:3.24 AS build
RUN apk add --no-cache build-base python3 bash
WORKDIR /src
COPY . .
ARG VERSION=dev
# Test the exact toolchain that produces the release binary, then build it.
RUN make test VERSION="$VERSION" && make clean && make static VERSION="$VERSION" \
 && ./build/healthcheck -V

FROM scratch
COPY --from=build /src/build/healthcheck /healthcheck
ENTRYPOINT ["/healthcheck"]
