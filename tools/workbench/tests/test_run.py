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


class PrepareCell(unittest.TestCase):
    def test_profile_is_copied_into_the_cell_and_never_passed_directly(self):
        # the engine writes settings back into the --config file; the source profile must stay untouched
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--only", "x"])
            cell = run.Cell("dev", sys.executable, "default", 0)
            cell_dir, cmd = run.prepare_cell(cell, args, out)
            cfg = cmd[cmd.index("--config") + 1]
            self.assertTrue(cfg.startswith(cell_dir))
            with open(cfg) as a, open(os.path.join(run.HERE, "profiles", "default.cfg")) as b:
                self.assertEqual(a.read(), b.read())


class ScanInfolog(unittest.TestCase):
    def test_flags_widget_load_failures_but_not_helper_modules(self):
        lines = [
            "[t=00:00:48.9][f=-000001] Failed to load: gui_gameinfo.lua  ([LuaVFS::Include] error=2 (attempt to call global 'setmetatable'))",
            "[t=00:00:49.0][f=-000001] Failed to load: tf_clone.lua  (no GetInfo() call)",
            "[t=00:00:50.0][f=0000010] [LuaRules] Error: gadget foo: attempt to index a nil value",
            "[t=00:00:51.0][f=0000011] Fatal: [ExitSpringProcess] errorMsg=\"boom\"",
            "[t=00:00:52.0][f=0000012] normal line",
        ]
        issues = run.scan_infolog(lines)
        kinds = [k for k, _ in issues]
        self.assertEqual(kinds, ["widget_load_failed", "lua_error", "fatal"])
        self.assertIn("gui_gameinfo.lua", issues[0][1])

    def test_flags_scenarios_that_failed_to_load_and_callin_errors(self):
        lines = [
            "[t=1][f=-000001] [Workbench] Error: failed to load workbench/scenarios/x.lua: [string]:3: '=' expected",
            "[t=2][f=0000010] [Workbench] Error: weapon_range_all UnitDamaged: attempt to index a nil value",
            "[t=3][f=0000002] [Workbench] Error: synced api_selftest.fail: intentional",
        ]
        kinds = [k for k, _ in run.scan_infolog(lines)]
        self.assertEqual(kinds, ["scenario_load_failed", "scenario_callin_error"])

    def test_duplicates_collapse(self):
        line = "[t=1][f=2] [LuaUI] Error: widget x: boom"
        self.assertEqual(len(run.scan_infolog([line, line.replace("[t=1]", "[t=9]")])), 1)


class Seed(unittest.TestCase):
    def test_seed_is_written_into_the_start_script(self):
        s = run.render_startscript("MapName=$MAP;\nFixedRNGSeed=$SEED;", "M", seed=1234)
        self.assertIn("FixedRNGSeed=1234;", s)

    def test_default_seed_zero_means_random(self):
        self.assertIn("FixedRNGSeed=0;", run.render_startscript("FixedRNGSeed=$SEED;", "M"))


class Spectate(unittest.TestCase):
    def test_spectate_uses_spectator_player_and_null_ai_teams(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--spectate"])
            cell_dir, cmd = run.prepare_cell(run.Cell("dev", sys.executable, "default", 0), args, out)
            with open(cmd[-1], encoding="utf-8") as f:
                script = f.read()
            self.assertIn("Spectator=1;", script)
            self.assertEqual(script.count("ShortName=NullAI;"), 2)

    def test_default_is_a_playing_player(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out])
            cell_dir, cmd = run.prepare_cell(run.Cell("dev", sys.executable, "default", 0), args, out)
            with open(cmd[-1], encoding="utf-8") as f:
                self.assertIn("Spectator=0;", f.read())


class Suites(unittest.TestCase):
    def test_suite_expands_to_its_scenarios(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--suite", "smoke"])
            self.assertEqual(args.only, run.SUITES["smoke"])

    def test_only_overrides_suite(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--suite", "smoke", "--only", "x"])
            self.assertEqual(args.only, "x")

    def test_render_suite_sweeps_settings_profiles(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--suite", "render"])
            self.assertEqual(args.profile, ["low", "default", "ultra"])

    def test_explicit_profile_overrides_suite_profiles(self):
        with tempfile.TemporaryDirectory() as out:
            args = run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--suite", "render",
                                   "--profile", "low"])
            self.assertEqual(args.profile, ["low"])

    def test_unknown_suite_is_an_error(self):
        with tempfile.TemporaryDirectory() as out, self.assertRaises(SystemExit):
            run.parse_args(["--engine", "dev=" + sys.executable, "--data-dir", out, "--suite", "nope"])


class CollectInfolog(unittest.TestCase):
    def test_stale_infolog_from_a_previous_run_is_not_attributed(self):
        with tempfile.TemporaryDirectory() as data, tempfile.TemporaryDirectory() as cell:
            log = os.path.join(data, "infolog.txt")
            with open(log, "w") as f:
                f.write("old run")
            os.utime(log, (1000, 1000))
            self.assertFalse(run.collect_infolog(data, cell, started=2000))
            self.assertFalse(os.path.exists(os.path.join(cell, "infolog.txt")))

    def test_fresh_infolog_is_copied(self):
        with tempfile.TemporaryDirectory() as data, tempfile.TemporaryDirectory() as cell:
            with open(os.path.join(data, "infolog.txt"), "w") as f:
                f.write("this run")
            self.assertTrue(run.collect_infolog(data, cell, started=0))
            self.assertTrue(os.path.exists(os.path.join(cell, "infolog.txt")))


if __name__ == "__main__":
    unittest.main()
