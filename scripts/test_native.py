"""Run hardware-independent tests using g++ (no connected ESP required)."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
output = root / ".pio" / "native-tests"
output.mkdir(parents=True, exist_ok=True)
for name, sources, includes in [
    ("light_controller", ["tests/light_controller_test.cpp"], ["include"]),
    ("firmware_upload", ["tests/firmware_upload_test.cpp"], ["include"]),
    ("network_manager", ["tests/network_manager_test.cpp", "src/NetworkManager.cpp"], ["tests/fakes", "include"]),
    ("configuration", ["tests/configuration_test.cpp", "src/Configuration.cpp"], ["tests/storage_fakes", ".pio/libdeps/esp07/ArduinoJson/src", "include"]),
]:
    binary = output / (name + (".exe" if os.name == "nt" else ""))
    command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    command += [f"-I{directory}" for directory in includes]
    if name == "configuration":
        command += ["-DARDUINOJSON_ENABLE_ARDUINO_STRING=1"]
    subprocess.run(command + sources + ["-o", str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
