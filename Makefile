VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion \
           -Werror -O2 -DHC_VERSION='"$(VERSION)"' $(EXTRA_CFLAGS)
LDFLAGS := $(EXTRA_LDFLAGS)

SRC      := src/url.c src/http.c src/net.c
HEADERS  := $(wildcard src/*.h)

all: build/healthcheck build/unit

build/healthcheck: $(SRC) src/main.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRC) src/main.c $(LDFLAGS)

build/unit: src/url.c src/http.c tests/unit.c $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ src/url.c src/http.c tests/unit.c $(LDFLAGS)

# Fully static, size-optimized binary. Needs musl (e.g. Alpine); see Dockerfile.
static: $(SRC) src/main.c $(HEADERS)
	@mkdir -p build
	$(CC) -std=c11 -Wall -Wextra -Werror -Os -DHC_VERSION='"$(VERSION)"' \
	  -static -s -o build/healthcheck $(SRC) src/main.c

test: all
	./build/unit
	./tests/integration.sh ./build/healthcheck

clean:
	rm -rf build

.PHONY: all static test clean
