import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import report  # noqa: E402


class Compare(unittest.TestCase):
    def test_same_within_threshold(self):
        self.assertEqual(report.compare([10.0, 10.2, 9.9], [10.1, 10.3, 10.0]), "same")

    def test_regression_beyond_threshold_and_spread(self):
        self.assertEqual(report.compare([10.0, 10.1, 9.9], [12.0, 12.1, 11.9]), "regression")

    def test_improvement(self):
        self.assertEqual(report.compare([10.0, 10.1, 9.9], [8.0, 8.1, 7.9]), "improvement")

    def test_noisy_delta_is_same(self):
        # 20% median delta, but the baseline spread is larger than the delta
        self.assertEqual(report.compare([5.0, 10.0, 15.0], [12.0, 12.0, 12.0]), "same")

    def test_missing_data(self):
        self.assertEqual(report.compare([], [1.0]), "n/a")


class Median(unittest.TestCase):
    def test_even_and_odd(self):
        self.assertEqual(report.median([3, 1, 2]), 2)
        self.assertEqual(report.median([4, 1, 2, 3]), 2.5)


class CompareSync(unittest.TestCase):
    def cs(self, pairs):
        return [{"frame": f, "checksum": c} for f, c in pairs]

    def test_identical(self):
        a = self.cs([(1, "aa"), (2, "bb")])
        self.assertEqual(report.compare_sync(a, list(a)), ("identical", None))

    def test_first_divergence_frame(self):
        a = self.cs([(1, "aa"), (2, "bb"), (3, "cc")])
        b = self.cs([(1, "aa"), (2, "XX"), (3, "YY")])
        self.assertEqual(report.compare_sync(a, b), ("diverged", 2))

    def test_shorter_run_diverges_where_it_ends(self):
        a = self.cs([(1, "aa"), (2, "bb")])
        b = self.cs([(1, "aa")])
        self.assertEqual(report.compare_sync(a, b), ("diverged", 2))

    def test_missing(self):
        self.assertEqual(report.compare_sync(None, self.cs([(1, "aa")])), ("n/a", None))


class MemorySeries(unittest.TestCase):
    def test_memory_metrics_are_read_from_memoryMB(self):
        cell = {"engine": "dev", "profile": "default", "scenarios": {"s": {"windows": [
            {"name": "w", "frameTimeMs": {"count": 1, "p50": 10.0},
             "memoryMB": {"start": 100.0, "peak": 400.0, "end": 250.0, "growth": 150.0}}]}}}
        self.assertEqual(report._metric_series([cell], "dev", "default", "s", "w", "memPeakMB"), [400.0])
        self.assertEqual(report._metric_series([cell], "dev", "default", "s", "w", "memGrowthMB"), [150.0])

    def test_old_results_without_memory_are_skipped(self):
        cell = {"engine": "dev", "profile": "default", "scenarios": {"s": {"windows": [
            {"name": "w", "frameTimeMs": {"count": 1, "p50": 10.0}}]}}}
        self.assertEqual(report._metric_series([cell], "dev", "default", "s", "w", "memPeakMB"), [])


if __name__ == "__main__":
    unittest.main()
