#!/usr/bin/env bash
# Integration tests: run the real binary against a local test server.
set -euo pipefail

BIN="${1:-./build/healthcheck}"
PORT=18080
CLOSED_PORT=18081 # nothing listens here

here="$(cd "$(dirname "$0")" && pwd)"
python3 "$here/server.py" "$PORT" &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true' EXIT

# Wait for the server using the binary under test (tcp mode).
for _ in $(seq 1 50); do
  "$BIN" -t 100 "tcp://127.0.0.1:$PORT" >/dev/null 2>&1 && break
  sleep 0.1
done

passed=0
failed=0

# expect <exit-code> <description> -- <args...>
expect() {
  local want="$1" name="$2"
  shift 3
  local got=0
  "$BIN" "$@" >/dev/null 2>&1 || got=$?
  if [ "$got" = "$want" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
    echo "FAIL: $name (want exit $want, got $got): $BIN $*"
  fi
}

base="http://127.0.0.1:$PORT"

expect 0 "200 is healthy"                  -- "$base/ok"
expect 0 "204 is healthy (HTTP/1.0)"       -- "$base/no-content"
expect 1 "503 is unhealthy"                -- "$base/fail"
expect 1 "404 is unhealthy"                -- "$base/missing"
expect 1 "302 is unhealthy by default"     -- "$base/redirect"
expect 0 "302 accepted with -s 200-399"    -- -s 200-399 "$base/redirect"
expect 0 "503 accepted with -s 503-503"    -- -s 503-503 "$base/fail"
expect 1 "garbage response"                -- "$base/garbage"
expect 1 "connection closed early"         -- "$base/close"
expect 0 "sends a correct Host header"     -- "$base/echo-host"
expect 1 "connection refused"              -- "http://127.0.0.1:$CLOSED_PORT/ok"
expect 0 "tcp connect succeeds"            -- "tcp://127.0.0.1:$PORT"
expect 1 "tcp connect refused"             -- "tcp://127.0.0.1:$CLOSED_PORT"
expect 1 "unknown host"                    -- -t 2000 "http://does-not-exist.invalid/ok"
expect 1 "invalid URL"                     -- "ftp://127.0.0.1/"
expect 1 "invalid timeout"                 -- -t 0 "$base/ok"
expect 1 "invalid status range"            -- -s 300-200 "$base/ok"
expect 1 "no URL at all"                   --
expect 1 "extra arguments"                 -- "$base/ok" "$base/ok"
expect 0 "-V prints version"               -- -V
expect 0 "-h prints help"                  -- -h

# URL and timeout from the environment (the Dockerfile-friendly form).
got=0; HEALTHCHECK_URL="$base/ok" "$BIN" >/dev/null 2>&1 || got=$?
[ "$got" = 0 ] && passed=$((passed + 1)) || { failed=$((failed + 1)); echo "FAIL: HEALTHCHECK_URL env"; }
got=0; HEALTHCHECK_TIMEOUT_MS=abc "$BIN" "$base/ok" >/dev/null 2>&1 || got=$?
[ "$got" = 1 ] && passed=$((passed + 1)) || { failed=$((failed + 1)); echo "FAIL: invalid HEALTHCHECK_TIMEOUT_MS"; }

# The timeout is a hard upper bound on the whole probe (/slow answers after 3s;
# the 2.5s bound leaves room for slow emulated builds, e.g. arm64 under QEMU).
start=$(python3 -c 'import time; print(int(time.monotonic() * 1000))')
got=0; "$BIN" -t 300 "$base/slow" >/dev/null 2>&1 || got=$?
elapsed=$(( $(python3 -c 'import time; print(int(time.monotonic() * 1000))') - start ))
if [ "$got" = 1 ] && [ "$elapsed" -lt 2500 ]; then
  passed=$((passed + 1))
else
  failed=$((failed + 1))
  echo "FAIL: timeout not enforced (exit $got after ${elapsed}ms)"
fi

# Verbose output format.
out=$("$BIN" -v "$base/ok" 2>&1)
case "$out" in
  "healthy: $base/ok (HTTP 200, "*) passed=$((passed + 1)) ;;
  *) failed=$((failed + 1)); echo "FAIL: verbose output: $out" ;;
esac

echo "integration: $passed passed, $failed failed"
[ "$failed" = 0 ]
