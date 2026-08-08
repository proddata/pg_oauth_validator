import base64
import http.server
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time


def b64(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


class Handler(http.server.BaseHTTPRequestHandler):
    issuer = None
    jwks = None
    protocol_version = "HTTP/1.0"

    def do_GET(self):
        value = ({"issuer": self.issuer, "jwks_uri": self.issuer + "/jwks"}
                 if self.path == "/metadata" else self.jwks)
        body = json.dumps(value, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def sign(key, issuer, now, temp, *, token_issuer=None, scope="connect:postgres",
         typ="at+jwt", kid="test-key", expiry=None):
    header = {"alg": "RS256", "typ": typ, "kid": kid}
    payload = {"iss": token_issuer or issuer, "aud": "postgres://primary",
               "exp": expiry if expiry is not None else now + 300,
               "sub": "principal", "scope": scope}
    signing_input = (b64(json.dumps(header, separators=(",", ":")).encode()) + "." +
                     b64(json.dumps(payload, separators=(",", ":")).encode()))
    input_file = temp / "signing-input"
    signature_file = temp / "signature"
    input_file.write_bytes(signing_input.encode())
    subprocess.run(["openssl", "dgst", "-sha256", "-sign", key,
                    "-out", signature_file, input_file], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return signing_input + "." + b64(signature_file.read_bytes())


def main():
    server = http.server.ThreadingHTTPServer(("localhost", 0), Handler)
    Handler.issuer = f"http://localhost:{server.server_port}/metadata"
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory() as directory:
        temp = pathlib.Path(directory)
        key = temp / "key.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-out", key,
                        "-pkeyopt", "rsa_keygen_bits:2048"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        modulus_output = subprocess.check_output(
            ["openssl", "rsa", "-in", key, "-noout", "-modulus"],
            stderr=subprocess.DEVNULL).decode().strip()
        modulus = bytes.fromhex(modulus_output.split("=", 1)[1])
        Handler.jwks = {"keys": [{"kty": "RSA", "n": b64(modulus), "e": "AQAB",
                                   "kid": "test-key", "alg": "RS256", "use": "sig"}]}
        now = int(time.time())
        valid = sign(key, Handler.issuer, now, temp)
        parts = valid.split(".")
        decoded = json.loads(base64.urlsafe_b64decode(parts[1] + "=="))
        decoded["sub"] = "attacker"
        tampered = parts[0] + "." + b64(json.dumps(decoded, separators=(",", ":")).encode()) + "." + parts[2]
        wrong_issuer = sign(key, Handler.issuer, now, temp,
                            token_issuer=Handler.issuer + "/other")
        missing_scope = sign(key, Handler.issuer, now, temp, scope="read:metadata")
        expired = sign(key, Handler.issuer, now, temp, expiry=now - 60)
        id_token = sign(key, Handler.issuer, now, temp, typ="JWT")
        unknown_key = sign(key, Handler.issuer, now, temp, kid="unknown")
        expected_identity = "v1." + b64(Handler.issuer.encode()) + "." + b64(b"principal")
        environment = os.environ.copy()
        environment.update({"HTTP_PROXY": "http://127.0.0.1:1", "NO_PROXY": ""})
        try:
            subprocess.run([pathlib.Path(sys.argv[1]).resolve(), Handler.issuer,
                            str(now), valid, tampered, wrong_issuer, missing_scope,
                            expired, id_token, unknown_key, expected_identity],
                           check=True, env=environment)
        finally:
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    main()
