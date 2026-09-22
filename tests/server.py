"""Test server for the integration tests.

Writes raw bytes on purpose, so it can also misbehave in ways a real HTTP
framework would not allow (garbage responses, closing early, never answering).

Usage: python3 server.py PORT
"""

import socket
import socketserver
import sys
import time

RESPONSES = {
    "/ok": b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok",
    "/no-content": b"HTTP/1.0 204 No Content\r\n\r\n",
    "/redirect": b"HTTP/1.1 302 Found\r\nLocation: /ok\r\n\r\n",
    "/fail": b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 4\r\n\r\nfail",
    "/garbage": b"this is not http at all\r\n",
}


class Handler(socketserver.BaseRequestHandler):
    def handle(self) -> None:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.request.recv(1024)
            if not chunk:
                return
            data += chunk
        path = data.split(b" ")[1].decode()

        if path == "/slow":
            time.sleep(3)
            self.request.sendall(RESPONSES["/ok"])
        elif path == "/close":
            self.request.shutdown(socket.SHUT_RDWR)
        elif path == "/echo-host":
            host = [line for line in data.split(b"\r\n") if line.lower().startswith(b"host:")]
            ok = host == [b"Host: 127.0.0.1:" + str(PORT).encode()]
            self.request.sendall(RESPONSES["/ok"] if ok else RESPONSES["/fail"])
        else:
            self.request.sendall(RESPONSES.get(path, b"HTTP/1.1 404 Not Found\r\n\r\n"))


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    PORT = int(sys.argv[1])
    with Server(("127.0.0.1", PORT), Handler) as server:
        server.serve_forever()
