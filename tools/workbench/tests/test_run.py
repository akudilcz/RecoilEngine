import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import run  # noqa: E402


class ExpandMatrix(unittest.TestCase):
    def test_cross_product_in_stable_order(self):
        cells = run.expand_matrix({"a": "a.exe", "b": "b.exe"}, ["low", "high"], 2)
        self.assertEqual(len(cells), 8)
        self.assertEqual(cells[0], run.Cell("a", "a.exe", "low", 0))
        self.assertEqual(cells[-1], run.Cell("b", "b.exe", "high", 1))


class StartScript(unittest.TestCase):
    def test_map_substituted_and_offline(self):
        s = run.render_startscript("MapName=$MAP;\nHostPort=0;", "Red Comet Remake 1.8")
        self.assertIn("MapName=Red Comet Remake 1.8;", s)
        self.assertIn("HostPort=0;", s)


class Status(unittest.TestCase):
    def test_exit_codes_map_to_status(self):
        self.assertEqual(run.status_for(0, timed_out=False), "ok")
        self.assertEqual(run.status_for(1, timed_out=False), "checks_failed")
        self.assertEqual(run.status_for(2, timed_out=False), "error")
        self.assertEqual(run.status_for(-1003, timed_out=False), "error")
        self.assertEqual(run.status_for(None, timed_out=True), "timeout")


class Tail(unittest.TestCase):
    def test_tail_missing_file_is_empty(self):
        self.assertEqual(run.tail_lines("/nonexistent/infolog.txt", 5), [])

    def test_tail_last_n(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write("\n".join(str(i) for i in range(100)))
        try:
            self.assertEqual(run.tail_lines(f.name, 3), ["97", "98", "99"])
        finally:
            os.unlink(f.name)


if __name__ == "__main__":
    unittest.main()
