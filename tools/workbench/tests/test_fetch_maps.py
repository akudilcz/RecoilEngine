import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import fetch_maps  # noqa: E402


class RankMaps(unittest.TestCase):
    def test_most_played_first_ties_by_name_and_missing_maps_ignored(self):
        replays = [{"Map": {"scriptName": n}} for n in ("B", "A", "C", "A", "B", "A")] + [{"Map": None}]
        self.assertEqual(fetch_maps.rank_maps(replays), [("A", 3), ("B", 2), ("C", 1)])
