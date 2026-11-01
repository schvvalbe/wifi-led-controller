"""Keep room for a second copy of the firmware with the project's 1m64 layout."""
from pathlib import Path
Import("env")


def check_ota_size(source, target, env):
    size = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin")).stat().st_size
    if size < 16:
        raise RuntimeError("Firmware image is missing or empty.")
    rounded = (size + 4095) & ~4095
    # _FS_start = 0x402EB000 in eagle.flash.1m64.ld. Keep the same extra
    # sector reserved by OtaManager::maxSize(). LittleFS is never overwritten.
    capacity = 0xEB000 - rounded - 4096
    print(f"OTA: image {size} bytes; space for next image {capacity} bytes")
    if size > capacity:
        raise RuntimeError("Firmware is too large to retain OTA support with the 1 MB / 64 KB layout.")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_ota_size)
env.AddPreAction("upload", check_ota_size)
