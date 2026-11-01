import importlib.util
import argparse
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("manifests", Path(__file__).with_name("manifests.py"))
manifests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifests)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.roots = {os: self.base / f"{os} payload" for os in ("windows", "macos", "linux")}
        files = {
            "windows": ["virtualjaguar.exe", "LICENSE.txt", "THIRD-PARTY-NOTICES.txt"],
            "macos": ["Virtual Jaguar.app/Contents/MacOS/virtualjaguar",
                      "Virtual Jaguar.app/Contents/Resources/LICENSE.txt",
                      "Virtual Jaguar.app/Contents/Resources/THIRD-PARTY-NOTICES.txt"],
            "linux": ["AppRun", "usr/bin/virtualjaguar",
                      "usr/share/doc/virtualjaguar/LICENSE.txt",
                      "usr/share/doc/virtualjaguar/THIRD-PARTY-NOTICES.txt"],
        }
        for os, names in files.items():
            for name in names:
                target = self.roots[os] / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("fixture")
                if name in ("AppRun", "usr/bin/virtualjaguar", "Virtual Jaguar.app/Contents/MacOS/virtualjaguar"):
                    target.chmod(0o755)
        self.depots = dict(zip(self.roots, ("101", "102", "103")))
        self.out = self.base / "scripts"

    def tearDown(self):
        self.temporary.cleanup()

    def generate(self, **kwargs):
        manifests.generate("100", self.depots, self.roots, self.out, 'Candidate "one"', **kwargs)

    def test_preview_is_default_and_never_sets_live(self):
        self.generate()
        app = (self.out / "app_build_100.vdf").read_text()
        self.assertIn('"Preview" "1"', app)
        self.assertIn('"SetLive" ""', app)
        self.assertIn('Candidate \\"one\\"', app)
        for os, depot in self.depots.items():
            data = (self.out / f"depot_build_{depot}.vdf").read_text()
            self.assertIn(manifests.quoted(self.roots[os].resolve().as_posix()), data)
            self.assertIn('"Recursive" "1"', data)
        self.assertEqual(len(list(self.out.iterdir())), 4)

    def test_upload_still_never_sets_live(self):
        self.generate(upload=True)
        app = (self.out / "app_build_100.vdf").read_text()
        self.assertIn('"Preview" "0"', app)
        self.assertIn('"SetLive" ""', app)

    def test_portable_marker_and_login_files_rejected(self):
        for filename in ("portable.txt", "config.vdf", "loginusers.vdf", "ssfn123456", ".env"):
            with self.subTest(filename=filename):
                marker = self.roots["windows"] / filename
                marker.write_text("must not upload")
                with self.assertRaisesRegex(ValueError, "forbidden"):
                    self.generate()
                self.assertFalse(self.out.exists())
                marker.unlink()

    def test_missing_notice_rejected(self):
        (self.roots["windows"] / "THIRD-PARTY-NOTICES.txt").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            self.generate()

    @unittest.skipIf(os.name == "nt", "POSIX executable mode is checked on POSIX hosts")
    def test_lost_linux_executable_mode_rejected(self):
        (self.roots["linux"] / "usr/bin/virtualjaguar").chmod(0o644)
        with self.assertRaisesRegex(ValueError, "not executable"):
            self.generate()

    def test_duplicate_ids_rejected(self):
        self.depots["macos"] = self.depots["windows"]
        with self.assertRaisesRegex(ValueError, "distinct"):
            self.generate()

    def test_output_inside_payload_rejected(self):
        self.out = self.roots["windows"] / "scripts"
        with self.assertRaisesRegex(ValueError, "separate"):
            self.generate()

    def test_existing_output_preserved(self):
        self.out.mkdir()
        marker = self.out / "keep"
        marker.write_text("existing work")
        with self.assertRaisesRegex(ValueError, "fresh"):
            self.generate()
        self.assertEqual(marker.read_text(), "existing work")

    def test_external_symlink_rejected_internal_symlink_allowed(self):
        link = self.roots["linux"] / "leak"
        try:
            link.symlink_to(self.base)
        except OSError:
            self.skipTest("host does not permit creating symlinks")
        with self.assertRaisesRegex(ValueError, "symlink leaves"):
            self.generate()
        link.unlink()
        link.symlink_to("usr/bin/virtualjaguar")
        self.generate()

    def test_invalid_assigned_id_and_control_characters(self):
        for value in ("0", "-1", "appid", "1\n", "１２３"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                manifests.positive_id(value)
        with self.assertRaisesRegex(ValueError, "Control"):
            manifests.quoted("invalid\npath")


if __name__ == "__main__":
    unittest.main()
