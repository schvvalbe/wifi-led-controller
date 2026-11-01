"""Release validation and publication tests; no network or real tags are used."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location(
    "ci_release", Path(__file__).resolve().parents[1] / "scripts/ci_release.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ["include", ".pio/build/esp07"]:
            (self.root / directory).mkdir(parents=True)
        (self.root / "include/FirmwareInfo.h").write_text(
            'constexpr char Version[] = "1.2.3";\n', encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text(
            "# [1.2.3] - 2026-11-01\n\n### Changes\n\n- New feature.\n\n"
            "# [1.2.2] - 2026-10-01\n\n- Old change.\n", encoding="utf-8")
        self.image = self.root / ".pio/build/esp07/firmware.bin"
        self.image.write_bytes(b"\xe9" + b"\0" * 99)
        self.sha = "a" * 40
        for patcher in [
            patch.object(ci, "ROOT", self.root),
            patch.object(ci, "DIST", self.root / "dist"),
            patch.object(ci, "git", side_effect=lambda *a: "" if a[0] == "tag" else self.sha),
            patch.dict(os.environ, GITHUB_SHA=self.sha, GITHUB_RUN_ID="123",
                       GITHUB_REPOSITORY="owner/repo"),
        ]:
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_notes_exclude_previous_release(self):
        version, notes = ci.metadata()
        self.assertEqual(version, "1.2.3")
        self.assertIn("New feature.", notes)
        self.assertNotIn("Old change.", notes)

    def test_mismatched_or_empty_changelog_rejected(self):
        for body in ["# [1.2.2] - 2026-11-01\n- Wrong version.",
                     "# [1.2.3] - 2026-11-01\n"]:
            (self.root / "CHANGELOG.md").write_text(body, encoding="utf-8")
            with self.assertRaises(ValueError):
                ci.metadata()

    def test_existing_version_rejected_in_pr(self):
        with patch.object(ci, "git", return_value=self.sha):
            with self.assertRaises(ValueError):
                ci.check(new_version=True)
            ci.check()  # A rerun on the same commit remains allowed.

    def test_package_and_checksums(self):
        ci.package()
        with zipfile.ZipFile(ci.DIST / "wifi-led-controller-1.2.3-esp07.zip") as archive:
            self.assertEqual(len(archive.namelist()), 3)
            self.assertEqual(archive.read("wifi-led-controller-1.2.3-esp07.bin"), self.image.read_bytes())
        for line in (ci.DIST / "SHA256SUMS").read_text().splitlines():
            checksum, name = line.split("  ")
            self.assertEqual(ci.digest((ci.DIST / name).read_bytes()), checksum)

    def test_invalid_or_oversized_firmware_rejected(self):
        for image in [b"", b"invalid image" * 10, b"\xe9" + b"\0" * 500000]:
            self.image.write_bytes(image)
            with self.assertRaises(ValueError):
                ci.package()

    def test_foreign_package_rejected_without_network(self):
        ci.package()
        with patch.dict(os.environ, GITHUB_SHA="b" * 40), patch.object(ci, "api") as api:
            with self.assertRaises(ValueError):
                ci.publish()
            api.assert_not_called()

    def test_corrupt_image_rejected_without_network(self):
        ci.package()
        (ci.DIST / "wifi-led-controller-1.2.3-esp07.bin").write_bytes(b"corrupt")
        with patch.object(ci, "api") as api:
            with self.assertRaises(ValueError):
                ci.publish()
            api.assert_not_called()

    def test_other_commit_tag_is_never_moved(self):
        ci.package()
        with patch.object(ci, "api", return_value={
            "object": {"type": "commit", "sha": "b" * 40}
        }) as api:
            with self.assertRaises(ValueError):
                ci.publish()
            self.assertEqual(api.call_count, 1)
            self.assertEqual(api.call_args.args[0], "GET")

    def test_uploads_finish_before_publication(self):
        ci.package()
        calls = []

        def fake_api(method, path, data=None, **kwargs):
            calls.append((method, path, data))
            if method == "GET":
                return None
            if method == "POST" and path.endswith("/releases"):
                self.assertTrue(data["draft"])
                return {"id": 9, "assets": [], "upload_url": "https://uploads.github.com/assets{?name}"}
            return {}

        with patch.object(ci, "api", side_effect=fake_api):
            ci.publish()
        self.assertEqual(sum("uploads.github.com" in c[1] for c in calls), 5)
        self.assertEqual(calls[-1][0], "PATCH")
        self.assertFalse(calls[-1][2]["draft"])
        self.assertEqual(calls[1][2]["sha"], self.sha)

    def test_upload_failure_keeps_draft(self):
        ci.package()
        calls = []

        def fake_api(method, path, data=None, **kwargs):
            calls.append(method)
            if method == "GET":
                return None
            if path.endswith("/releases"):
                return {"id": 9, "assets": [], "upload_url": "https://uploads.github.com/assets{?name}"}
            if "uploads.github.com" in path:
                raise RuntimeError("upload failed")
            return {}

        with patch.object(ci, "api", side_effect=fake_api):
            with self.assertRaises(RuntimeError):
                ci.publish()
        self.assertNotIn("PATCH", calls)

    def test_published_same_commit_is_unchanged(self):
        ci.package()
        assets = [{"name": p.name} for p in ci.DIST.iterdir()]
        with patch.object(ci, "api", side_effect=[
            {"object": {"type": "commit", "sha": self.sha}},
            {"draft": False, "assets": assets},
        ]) as api:
            ci.publish()
            self.assertTrue(all(call.args[0] == "GET" for call in api.call_args_list))

    def test_draft_retry_replaces_assets_before_publication(self):
        ci.package()
        calls = []

        def fake_api(method, path, data=None, **kwargs):
            calls.append((method, path))
            if "/git/ref/" in path:
                return {"object": {"type": "commit", "sha": self.sha}}
            if "/releases/tags/" in path:
                return {"draft": True, "id": 9, "assets": [{"id": 7}],
                        "upload_url": "https://uploads.github.com/assets{?name}"}
            return {}

        with patch.object(ci, "api", side_effect=fake_api):
            ci.publish()
        self.assertEqual(calls[2][0], "DELETE")
        self.assertEqual(calls[-1][0], "PATCH")


if __name__ == "__main__":
    unittest.main()

