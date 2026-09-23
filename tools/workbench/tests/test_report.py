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


class Timers(unittest.TestCase):
    def cell(self, engine, path_ms):
        return {"engine": engine, "profile": "default", "scenarios": {"s": {"windows": [
            {"name": "w", "frameTimeMs": {"count": 1, "p50": 1.0},
             "timers": [{"name": "Sim::Path", "totalMs": path_ms, "perSimFrameMs": path_ms / 10, "perDrawFrameMs": path_ms / 20},
                        {"name": "Draw", "totalMs": 5.0, "perSimFrameMs": 0.5, "perDrawFrameMs": 0.25}]}]}}}

    def test_top_timers_by_baseline_cost(self):
        cells = [self.cell("base", 100.0), self.cell("dev", 60.0)]
        rows = report.timer_rows(cells, ["base", "dev"], "default", "s", "w", top=5)
        self.assertEqual(rows[0][0], "Sim::Path")
        self.assertEqual(rows[0][1], [10.0, 6.0])  # median perSimFrameMs per engine

    def test_missing_timers_are_none(self):
        cells = [self.cell("base", 100.0), {"engine": "dev", "profile": "default", "scenarios": {}}]
        rows = report.timer_rows(cells, ["base", "dev"], "default", "s", "w", top=5)
        self.assertEqual(rows[0][1], [10.0, None])


class SyncBaseline(unittest.TestCase):
    def test_baseline_is_the_first_engines_first_cell(self):
        cells = [{"engine": "base", "sync": None}, {"engine": "dev", "sync": {"checksums": [1]}},
                 {"engine": "dev", "sync": {"checksums": [2]}}]
        ref, label = report.sync_reference(cells, "base")
        self.assertEqual(ref, [1])
        self.assertIn("baseline engine has no", label)

    def test_baseline_engine_used_when_present(self):
        cells = [{"engine": "base", "sync": {"checksums": [9]}}, {"engine": "dev", "sync": {"checksums": [1]}}]
        ref, label = report.sync_reference(cells, "base")
        self.assertEqual(ref, [9])
        self.assertEqual(label, "base")


if __name__ == "__main__":
    unittest.main()
