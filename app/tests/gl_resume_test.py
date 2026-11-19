"""Regression tests for retaining individual passing GL results across a failed shard."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("gl_qualify", Path(__file__).resolve().parents[1] / "test/gl-qualification/gl_qualify.py")
gl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gl)


class ResumeTests(unittest.TestCase):
    def test_matching_evidence_keeps_passes_and_retries_failures(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'logs').mkdir()
            (root / 'shots').mkdir()
            (root / 'logs/good.log').write_text('shader built')
            (root / 'shots/good.png').write_bytes(b'captured bytes')
            (root / 'logs/slow.log').write_text('known slow timeout')
            report = root / 'results.json'
            data = {'checkpoint_key': 'inputs', 'os': 'macos', 'shard': '0/1', 'selected': ['good', 'bad', 'slow'],
                    'results': [
                        {'preset': 'good', 'errors': [], 'verdict': 'built', 'log': '/old/logs/good.log', 'png': '/old/shots/good.png'},
                        {'preset': 'bad', 'errors': ['timeout'], 'verdict': 'failed'},
                        {'preset': 'slow', 'errors': [], 'verdict': 'slow-nochain', 'slow_capped': 120,
                         'log': '/old/logs/slow.log', 'png': '/old/shots/slow.png'}]}
            report.write_text(json.dumps(data))
            results = gl.resume_results(report, 'inputs', 'macos', '0/1', data['selected'])
            self.assertEqual(set(results), {'good', 'slow'})
            self.assertTrue(results['good']['reused'])
            self.assertEqual(results['good']['log'], str(root / 'logs/good.log'))
            self.assertEqual(results['slow']['png'], '')
            for key, os_name, shard, selected in [('', 'macos', '0/1', data['selected']),
                                                 ('different', 'macos', '0/1', data['selected']),
                                                 ('inputs', 'linux', '0/1', data['selected']),
                                                 ('inputs', 'macos', '1/2', data['selected']),
                                                 ('inputs', 'macos', '0/1', ['different'])]:
                with self.assertRaises(ValueError):
                    gl.resume_results(report, key, os_name, shard, selected)
            (root / 'shots/good.png').unlink()
            with self.assertRaisesRegex(ValueError, 'missing'):
                gl.resume_results(report, 'inputs', 'macos', '0/1', data['selected'])

    def test_timeout_retries_run_serially_and_repeated_failures_stay_failed(self):
        calls = []
        results = [{'preset': 'good', 'errors': [], 'verdict': 'built'},
                   {'preset': 'bad-compile', 'errors': ['compile error']},
                   {'preset': 'timeout', 'errors': ['timeout'], 'timed_out': 240, 'seconds': 240}]
        def passing_one(preset):
            calls.append(preset)
            return {'preset': preset, 'errors': [], 'verdict': 'built', 'notes': []}
        retried = gl.retry_timeouts(results, passing_one)
        self.assertEqual(calls, ['timeout'])
        self.assertEqual(retried[0], results[0])
        self.assertEqual(retried[1], results[1])
        self.assertEqual(retried[2]['errors'], [])
        self.assertEqual(retried[2]['parallel_attempt']['errors'], ['timeout'])
        def failing_one(preset):
            return {'preset': preset, 'errors': ['timeout again'], 'verdict': 'failed', 'notes': []}
        retried = gl.retry_timeouts(results, failing_one)
        self.assertEqual(retried[2]['errors'], ['timeout again'])

    def test_no_report_starts_fresh(self):
        self.assertEqual(gl.resume_results(Path('/no-such-report.json'), None, 'macos', '0/1', []), {})


if __name__ == '__main__':
    unittest.main()
