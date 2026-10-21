import copy
import unittest
from qualification_support import verify_release_unchanged


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.release = {'id': 1, 'target_commitish': 'expected', 'assets': [
            {'id': 2, 'name': 'app.zip', 'size': 100, 'digest': 'sha256:old', 'updated_at': 'old'}]}

    def test_same_snapshot_passes(self):
        verify_release_unchanged(self.release, copy.deepcopy(self.release), 'expected')

    def test_source_republished_during_install_fails(self):
        after = copy.deepcopy(self.release)
        after['target_commitish'] = 'another-candidate'
        with self.assertRaisesRegex(RuntimeError, 'source changed'):
            verify_release_unchanged(self.release, after, 'expected')

    def test_assets_replaced_even_on_same_commit_fail(self):
        for key, value in [('id', 3), ('size', 101), ('digest', 'sha256:new'), ('updated_at', 'new')]:
            after = copy.deepcopy(self.release)
            after['assets'][0][key] = value
            with self.assertRaisesRegex(RuntimeError, 'assets changed'):
                verify_release_unchanged(self.release, after, 'expected')


if __name__ == '__main__':
    unittest.main()
