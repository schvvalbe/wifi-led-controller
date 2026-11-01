"""Validate, package and publish firmware. Only 'publish' accesses GitHub."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True).strip()


def metadata():
    header = (ROOT / "include/FirmwareInfo.h").read_text(encoding="utf-8")
    versions = re.findall(r'^\s*constexpr\s+char\s+Version\[\]\s*=\s*"(\d+\.\d+\.\d+)";', header, re.M)
    if len(versions) != 1:
        raise ValueError("FirmwareInfo.h must define exactly one numeric X.Y.Z Version.")
    version = versions[0]
    changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    sections = list(re.finditer(r"^# \[([^\]]+)\] - (\d{4}-\d{2}-\d{2})\s*$", changelog, re.M))
    if not sections or sections[0][1] != version:
        raise ValueError("The first CHANGELOG.md section must match FirmwareInfo.h.")
    if sum(section[1] == version for section in sections) != 1:
        raise ValueError("Duplicate version in CHANGELOG.md.")
    end = sections[1].start() if len(sections) > 1 else len(changelog)
    notes = changelog[sections[0].end():end].strip()
    if not notes or not re.search(r"^\s*-\s+\S", notes, re.M):
        raise ValueError("Release notes must contain at least one change.")
    return version, notes + "\n"


def check(new_version=False):
    version, notes = metadata()
    tag = "v" + version
    if git("tag", "--list", tag):
        target = git("rev-parse", tag + "^{commit}")
        if new_version or target != git("rev-parse", "HEAD"):
            raise ValueError(f"{tag} already exists. Bump FirmwareInfo.h and add a CHANGELOG section.")
    print(f"Release version: {version}")
    return version, notes


def digest(data):
    return hashlib.sha256(data).hexdigest()


def package():
    version, notes = check()
    image = (ROOT / ".pio/build/esp07/firmware.bin").read_bytes()
    if len(image) < 16 or image[0] != 0xE9:
        raise ValueError("Missing or invalid ESP8266 firmware image.")
    # Same 1m64 OTA budget as scripts/check_ota_size.py.
    if len(image) > 0xEB000 - ((len(image) + 4095) & ~4095) - 4096:
        raise ValueError("Firmware exceeds the OTA size budget.")
    if DIST.exists() and any(DIST.iterdir()):
        raise ValueError("dist/ must be empty before packaging.")
    DIST.mkdir(exist_ok=True)
    stem = f"wifi-led-controller-{version}-esp07"
    manifest = {
        "version": version, "commit": git("rev-parse", "HEAD"),
        "run_id": os.environ.get("GITHUB_RUN_ID", "local"),
        "board": "esp07", "environment": "esp07",
        "firmware": stem + ".bin", "size": len(image), "sha256": digest(image),
    }
    files = {
        stem + ".bin": image,
        "build-info.json": (json.dumps(manifest, indent=2) + "\n").encode(),
        "release-notes.md": notes.encode(),
    }
    for name, data in files.items():
        (DIST / name).write_bytes(data)
    archive = DIST / (stem + ".zip")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, data in files.items():
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(info, data)
    sums = "".join(f"{digest(path.read_bytes())}  {path.name}\n"
                   for path in sorted(DIST.iterdir()))
    (DIST / "SHA256SUMS").write_text(sums, encoding="utf-8", newline="\n")
    print(f"Packaged {len(image)} bytes in {archive.name}")


def api(method, path, data=None, missing_ok=False, binary=False):
    url = path if path.startswith("https://") else "https://api.github.com/" + path
    payload = data if binary else (json.dumps(data).encode() if data is not None else None)
    request = urllib.request.Request(url, data=payload, method=method, headers={
        "Authorization": "Bearer " + os.environ["GH_TOKEN"],
        "Accept": "application/vnd.github+json",
        "Content-Type": "application/octet-stream" if binary else "application/json",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    try:
        with urllib.request.urlopen(request, timeout=90) as response:
            body = response.read()
            return json.loads(body) if body else None
    except urllib.error.HTTPError as error:
        if missing_ok and error.code == 404:
            return None
        # Do not log request headers or credentials.
        raise RuntimeError(f"GitHub {method} failed: HTTP {error.code}") from None


def publish():
    version, notes = metadata()
    sha = os.environ["GITHUB_SHA"]
    repository = os.environ["GITHUB_REPOSITORY"]
    manifest = json.loads((DIST / "build-info.json").read_text(encoding="utf-8"))
    if (manifest["commit"] != sha or manifest["version"] != version
            or manifest["run_id"] != os.environ["GITHUB_RUN_ID"]):
        raise ValueError("Package provenance does not match this workflow.")
    stem = f"wifi-led-controller-{version}-esp07"
    expected = {stem + ".bin", stem + ".zip", "build-info.json", "release-notes.md"}
    lines = (DIST / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    sums = dict(line.split("  ", 1)[::-1] for line in lines)
    if set(sums) != expected or len(lines) != len(expected):
        raise ValueError("Unexpected package contents in SHA256SUMS.")
    for name, checksum in sums.items():
        if digest((DIST / name).read_bytes()) != checksum:
            raise ValueError(f"Checksum mismatch: {name}")
    image = (DIST / (stem + ".bin")).read_bytes()
    if digest(image) != manifest["sha256"] or len(image) != manifest["size"]:
        raise ValueError("Firmware does not match build-info.json.")
    if (DIST / "release-notes.md").read_text(encoding="utf-8") != notes:
        raise ValueError("Packaged release notes do not match CHANGELOG.md.")
    tag = "v" + version
    base = f"repos/{repository}"
    ref = api("GET", f"{base}/git/ref/tags/{tag}", missing_ok=True)
    if ref:
        obj = ref["object"]
        while obj["type"] == "tag":
            obj = api("GET", f"{base}/git/tags/{obj['sha']}")["object"]
        if obj["type"] != "commit" or obj["sha"] != sha:
            raise ValueError(f"{tag} points to another commit; bump the version.")
    else:
        api("POST", f"{base}/git/refs", {"ref": "refs/tags/" + tag, "sha": sha})
    release = api("GET", f"{base}/releases/tags/{tag}", missing_ok=True)
    assets = expected | {"SHA256SUMS"}
    if release and not release["draft"]:
        if {asset["name"] for asset in release["assets"]} != assets:
            raise ValueError("Existing published release has unexpected assets; inspect it manually.")
        print(f"{tag} is already published for this commit; leaving it unchanged.")
        return
    if not release:
        release = api("POST", f"{base}/releases", {
            "tag_name": tag, "target_commitish": sha, "name": tag,
            "body": notes, "draft": True, "prerelease": False,
        })
    # Resume an interrupted draft only after checking its tag's commit above.
    for asset in release["assets"]:
        api("DELETE", f"{base}/releases/assets/{asset['id']}")
    upload_url = release["upload_url"].split("{", 1)[0]
    for name in sorted(assets):
        api("POST", upload_url + "?name=" + urllib.parse.quote(name),
            (DIST / name).read_bytes(), binary=True)
    api("PATCH", f"{base}/releases/{release['id']}", {
        "draft": False, "body": notes, "name": tag,
    })
    print(f"Published {tag}: https://github.com/{repository}/releases/tag/{tag}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["check", "package", "publish"])
    parser.add_argument("--new-version", action="store_true")
    args = parser.parse_args()
    if args.command == "check":
        check(args.new_version)
    elif args.command == "package":
        package()
    else:
        publish()

