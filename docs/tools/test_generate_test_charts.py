"""Checks for exclusions that could otherwise overstate a continuous run."""
import math
import unittest

from generate_test_charts import COUNTERS, continuous_runs, fresh_value


def sample(epoch, uptime=None, counter=0):
    return {"valid": True, "epoch": epoch,
            "diagnostics": dict.fromkeys(COUNTERS, counter) | {
                "uptime_seconds": epoch if uptime is None else uptime}}


class SelectionTests(unittest.TestCase):
    def test_failed_response_breaks_run(self):
        runs = continuous_runs([sample(0), sample(10), {"valid": False}, sample(30)])
        self.assertEqual([len(run) for run in runs], [2, 1])

    def test_long_gap_breaks_run(self):
        runs = continuous_runs([sample(0), sample(10), sample(40), sample(50)])
        self.assertEqual([len(run) for run in runs], [2, 2])

    def test_uptime_reset_breaks_run(self):
        runs = continuous_runs([sample(0, 100), sample(10, 110), sample(20, 3)])
        self.assertEqual([len(run) for run in runs], [2, 1])

    def test_counter_decrease_breaks_run(self):
        runs = continuous_runs([sample(0, counter=4), sample(10, counter=5), sample(20)])
        self.assertEqual([len(run) for run in runs], [2, 1])

    def test_sensor_error_is_missing_not_zero(self):
        state = {"LIGHT_A": 50, "ldr_valid": True, "ldr_error": 263}
        self.assertTrue(math.isnan(fresh_value(state, "LIGHT_A")))
        state["ldr_error"] = 0
        self.assertEqual(fresh_value(state, "LIGHT_A"), 50)
        state["ldr_valid"] = False
        self.assertTrue(math.isnan(fresh_value(state, "LIGHT_A")))


if __name__ == "__main__":
    unittest.main()
