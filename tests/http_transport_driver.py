import http.server
import os
import pathlib
import ssl
import subprocess
import sys
import tempfile
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            body = b"{}"
        elif self.path == "/large":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            body = b'{"x":"' + b'x' * 128 + b'"}'
        elif self.path == "/text":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            body = b'{"ok":true}'
        elif self.path == "/jwks":
            self.send_response(200)
            self.send_header("Content-Type", "Application/JWK-Set+JSON")
            body = b'{"keys":[]}'
        elif self.path == "/fresh":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "public")
            self.send_header("Cache-Control", "max-age=120")
            self.send_header("Age", "20")
            self.send_header("Expires", "Sun, 06 Nov 1994 08:51:37 GMT")
            body = b'{"ok":true}'
        elif self.path == "/large-freshness-header":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "x" * 5000)
            body = b'{"ok":true}'
        elif self.path == "/error":
            self.send_response(503)
            self.send_header("Content-Type", "application/json")
            body = b'{}'
        elif self.path == "/slow":
            time.sleep(0.3)
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            body = b'{"ok":true}'
        else:
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            body = b'{"ok":true}'
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ssl.SSLEOFError):
            pass

    def log_message(self, *_args):
        pass


def serve(tls_context=None):
    server = http.server.ThreadingHTTPServer(("localhost", 0), Handler)
    if tls_context is not None:
        server.socket = tls_context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        temp = pathlib.Path(directory)
        key = temp / "key.pem"
        cert = temp / "cert.pem"
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=localhost",
             "-addext", "subjectAltName=DNS:localhost"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        http_server, http_thread = serve()
        https_server, https_thread = serve(context)
        try:
            environment = os.environ.copy()
            environment.update({"HTTP_PROXY": "http://127.0.0.1:1",
                                "HTTPS_PROXY": "http://127.0.0.1:1",
                                "ALL_PROXY": "http://127.0.0.1:1",
                                "NO_PROXY": ""})
            subprocess.run(
                [binary, f"http://localhost:{http_server.server_port}",
                 f"https://localhost:{https_server.server_port}", cert],
                check=True, env=environment)
        finally:
            http_server.shutdown()
            https_server.shutdown()
            http_thread.join()
            https_thread.join()


if __name__ == "__main__":
    main()
