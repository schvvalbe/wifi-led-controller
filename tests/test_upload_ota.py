"""Exercise the real uploader against a local HTTP server; never touch hardware."""
from contextlib import redirect_stdout
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import importlib.util
import io
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("upload_ota", Path(__file__).resolve().parents[1] / "scripts/upload_ota.py")
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


class UploadTest(unittest.TestCase):
    def setUp(self):
        self.posts = []
        self.mode = "ok"
        self.limit = 100000
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args): pass

            def reply(self, status, body):
                data = json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                owner.assertEqual(self.path, "/api/ota")
                self.reply(200, {"maxSize": owner.limit, "version": "1.1.0", "busy": False})

            def do_POST(self):
                owner.posts.append((self.path, dict(self.headers), self.rfile.read(int(self.headers["Content-Length"]))))
                if owner.mode == "disconnect":
                    self.connection.shutdown(socket.SHUT_RDWR)
                    self.connection.close()
                elif owner.mode == "reject":
                    self.reply(403, {"error": "Wrong password"})
                else:
                    self.reply(202, {"rebooting": True})

        self.server = HTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.host = f"127.0.0.1:{self.server.server_port}"
        self.directory = tempfile.TemporaryDirectory()
        self.file = Path(self.directory.name) / "firmware.bin"
        self.data = b"\xe9" + bytes(range(256)) * 10
        self.file.write_bytes(self.data)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.directory.cleanup()

    def run_upload(self):
        with patch.object(ota.time, "sleep"), redirect_stdout(io.StringIO()):
            return ota.upload(self.host, self.file, "test-password")

    def test_valid_transfer_and_restart_check(self):
        self.assertEqual(self.run_upload(), 0)
        self.assertEqual(len(self.posts), 1)
        path, headers, body = self.posts[0]
        self.assertEqual(path, "/api/ota")
        self.assertEqual(headers["X-OTA-Password"], "test-password")
        self.assertEqual(headers["X-Firmware-Size"], str(len(self.data)))
        self.assertEqual(headers["X-Firmware-MD5"], hashlib.md5(self.data).hexdigest())
        self.assertIn(self.data, body)

    def test_rejection_does_not_retry(self):
        self.mode = "reject"
        with self.assertRaisesRegex(ValueError, "Wrong password"):
            self.run_upload()
        self.assertEqual(len(self.posts), 1)

    def test_connection_loss_does_not_retry(self):
        self.mode = "disconnect"
        with self.assertRaisesRegex(RuntimeError, "result unknown"):
            self.run_upload()
        self.assertEqual(len(self.posts), 1)

    def test_oversized_or_wrong_image_never_sent(self):
        self.limit = 100
        with self.assertRaises(ValueError): self.run_upload()
        self.limit = 100000
        self.file.write_bytes(b"not firmware" * 10)
        with self.assertRaises(ValueError): self.run_upload()
        self.assertEqual(self.posts, [])


if __name__ == "__main__":
    unittest.main()
