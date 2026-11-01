"""Prevent PlatformIO ESP8266 4.2.1's IP heuristic from selecting espota."""
Import("env")

port = env.get("UPLOAD_PORT") or env.GetProjectOption("upload_port", "192.168.4.1")
if "://" not in port:
    port = "http://" + port
env.Replace(UPLOAD_PORT=port)
