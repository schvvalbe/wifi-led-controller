"""Upload this project's firmware using the same HTTP OTA API as the web UI.

No third-party Python packages, espota, uploadfs or cloud service are required.
The POST is sent exactly once; a lost response is never retried automatically.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import sys
import time
from urllib.parse import urlsplit


def connection(host, timeout):
    url = urlsplit(host if "://" in host else "http://" + host)
    if (url.scheme not in ("http", "https") or not url.hostname or
            url.username or url.password or url.path not in ("", "/") or url.query or url.fragment):
        raise ValueError("Host must be a controller IP/name or an http(s) URL without a path.")
    cls = http.client.HTTPSConnection if url.scheme == "https" else http.client.HTTPConnection
    return cls(url.hostname, url.port, timeout=timeout)


def metadata(host, timeout=5):
    conn = connection(host, timeout)
    try:
        conn.request("GET", "/api/ota", headers={"Cache-Control": "no-store"})
        response = conn.getresponse()
        data = response.read(4096)
        if response.status != 200:
            raise ValueError("Controller does not expose OTA. Install an OTA-capable firmware over USB first.")
        return json.loads(data)
    finally:
        conn.close()


def upload(host, filename, password):
    path = Path(filename)
    info = metadata(host)
    size = path.stat().st_size
    if info.get("busy"):
        raise ValueError("Controller is already updating or restarting.")
    if path.suffix.lower() != ".bin" or not 16 <= size <= info.get("maxSize", 0):
        raise ValueError(f"Invalid firmware size/type. Controller limit: {info.get('maxSize', 0)} bytes.")
    data = path.read_bytes()
    if len(data) != size or data[0] != 0xE9:
        raise ValueError("File changed while reading or is not an uncompressed ESP8266 firmware image.")
    if not password or not password.isascii() or any(ord(ch) < 32 for ch in password):
        raise ValueError("OTA password must contain printable ASCII characters.")
    digest = hashlib.md5(data).hexdigest()  # Transfer integrity, not a signature.
    boundary = "WiFiLedController" + os.urandom(16).hex()
    body = (f'--{boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
            'Content-Type: application/octet-stream\r\n\r\n').encode() + data + f"\r\n--{boundary}--\r\n".encode()
    print(f"Uploading {size} bytes to {host} (current version {info.get('version', '?')})", flush=True)
    conn = connection(host, 180)
    try:
        conn.putrequest("POST", "/api/ota")
        for key, value in {
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Content-Length": str(len(body)),
            "X-OTA-Password": password,
            "X-Firmware-Size": str(size),
            "X-Firmware-MD5": digest,
            "Connection": "close",
        }.items():
            conn.putheader(key, value)
        conn.endheaders()
        for offset in range(0, len(body), 16384):
            conn.send(body[offset:offset + 16384])
            print(f"\rSent {min(offset + 16384, len(body)) * 100 // len(body)}%", end="", flush=True)
        response = conn.getresponse()
        result = json.loads(response.read(4096))
        print()
        if response.status != 202 or not result.get("rebooting"):
            raise ValueError(result.get("error", f"Upload rejected: HTTP {response.status}"))
    except (OSError, http.client.HTTPException, json.JSONDecodeError) as error:
        raise RuntimeError("Upload result unknown after a connection failure. Wait, check the controller/version before retrying. "
                           "The firmware has NOT been sent a second time.") from error
    finally:
        conn.close()
    print("Firmware verified and accepted. Waiting for reboot...", flush=True)
    time.sleep(5)
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        try:
            current = metadata(host, timeout=3)
            if not current.get("busy") and current.get("version"):
                print(f"Controller is back online. Firmware version: {current['version']}")
                return 0
        except (OSError, ValueError, http.client.HTTPException):
            pass
        time.sleep(2)
    print("Firmware was accepted, but reboot could not be verified. Reconnect to the controller and check its version. Do not blindly repeat the upload.", file=sys.stderr)
    return 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="controller IP or hostname, e.g. 192.168.4.1")
    parser.add_argument("--file", required=True, type=Path, help="firmware.bin from this project's build")
    args = parser.parse_args()
    try:
        return upload(args.host, args.file, os.environ.get("WIFI_LED_OTA_PASSWORD", "wifi-led-controller"))
    except (OSError, ValueError, RuntimeError, http.client.HTTPException) as error:
        print(f"OTA: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
