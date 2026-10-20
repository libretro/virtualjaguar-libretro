"""Synthetic tests for the pinned community-repository integration runner."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import community_test as runner
from test_convert import source, SIMPLE, DIGEST, ROM


class CommunityTests(unittest.TestCase):
    def setUp(self):
        self.raw = source(SIMPLE).encode("utf-8")
        module = runner.convert.Module(self.raw.decode())
        self.case = {"source": "scripts/cheat_synthetic.c", "source_sha256": hashlib.sha256(self.raw).hexdigest(),
                     "rom_fnv1a64": f"{DIGEST:016X}", "rom_name": "synthetic.j64",
                     "title": module.title, "expected": runner.contract(module, DIGEST)}

    def test_changed_source_refused_before_parsing(self):
        with self.assertRaisesRegex(ValueError, "checksum"):
            runner.check_case(self.raw + b" broken C", self.case)

    def test_changed_conversion_contract_fails(self):
        runner.check_case(self.raw, self.case)
        self.case["expected"][0]["writes"] += 1
        with self.assertRaisesRegex(ValueError, "contract changed"):
            runner.check_case(self.raw, self.case)

    def test_rom_mode_requires_real_matching_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(OSError):
                runner.check_case(self.raw, self.case, root)
            path = root / self.case["rom_name"]
            path.write_bytes(ROM)
            runner.check_case(self.raw, self.case, root)
            path.write_bytes(ROM + b"different revision")
            with self.assertRaisesRegex(ValueError, "ROM hash"):
                runner.check_case(self.raw, self.case, root)

    def test_missing_source_and_network_error_fail_instead_of_skip(self):
        manifest = {"format": 1, "repository": "example/author-cheats", "commit": "a"*40, "cases": [self.case]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest))
            with mock.patch.object(runner, "MANIFEST", path), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(runner.main(["--source-dir", directory]), 1)
                with mock.patch.object(runner.urllib.request, "urlopen", side_effect=OSError("offline")):
                    self.assertEqual(runner.main(["--fetch"]), 1)
                with mock.patch.object(runner.urllib.request, "urlopen", return_value=io.BytesIO(self.raw)):
                    self.assertEqual(runner.main(["--fetch"]), 0)


if __name__ == "__main__":
    unittest.main()
