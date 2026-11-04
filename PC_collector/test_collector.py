import contextlib
import csv
from datetime import datetime
from http.client import IncompleteRead
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import URLError
from urllib.request import ProxyHandler, build_opener

import collector


def state_fixture():
    return {
        "pump": 1, "LIGHT_S": 55, "LIGHT_A": 19, "AUTO": 1, "MOD": 2,
        "temperature": 23.25, "humidity": 44.5, "soil_percent": 67,
        "water_percent": 80, "wifi_connected": True, "time_synced": True,
        "htu21_valid": True, "soil_valid": True, "ldr_valid": True, "water_valid": True,
        "htu21_error": 0, "soil_error": 0, "ldr_error": 0, "water_error": 0,
    }


def diagnostics_fixture():
    values = {key: 0 for key in collector.DIAGNOSTIC_FIELDS}
    values.update(uptime_seconds=1234, sensor_cycles=500, heap_free_bytes=100000,
                  heap_min_free_bytes=90000, stack_control_bytes=2048)
    return values


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        status, body = self.server.responses[self.path]
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class CollectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.host = f"127.0.0.1:{cls.server.server_port}"
        cls.opener = build_opener(ProxyHandler({}))

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        self.server.responses = {
            "/api/state": (200, json.dumps(state_fixture()).encode()),
            "/api/diagnostics": (200, json.dumps(diagnostics_fixture()).encode()),
        }

    def test_flatten_nested_and_arrays(self):
        self.assertEqual(collector.flatten_json({"stack": {"control": 1000}, "a": [2, None]}, "diag"),
                         {"diag_stack_control": 1000, "diag_a_0": 2, "diag_a_1": None})
        with self.assertRaises(ValueError):
            collector.flatten_json({"a_b": 1, "a": {"b": 2}})

    def test_valid_schema_nulls_and_uint64(self):
        state = state_fixture()
        state.update(temperature=None, humidity=None, htu21_valid=False, htu21_error=263)
        self.assertIsNone(collector.validate_response(state, "state")["temperature"])
        diag = diagnostics_fixture()
        diag["sensor_cycles"] = 2**64 - 1
        self.assertEqual(collector.validate_response(diag, "diagnostics")["sensor_cycles"], 2**64 - 1)

    def test_invalid_types_and_missing_fields(self):
        for key, value in (("MOD", 3), ("pump", True), ("LIGHT_S", -1),
                           ("wifi_connected", 1), ("temperature", float("nan"))):
            with self.subTest(key=key):
                data = state_fixture()
                data[key] = value
                with self.assertRaises(ValueError):
                    collector.validate_response(data, "state")
        with self.assertRaises(ValueError):
            collector.validate_response({}, "diagnostics")
        with self.assertRaises(ValueError):
            collector.validate_response([], "state")
        data = diagnostics_fixture()
        data["sensor_cycles"] = -1
        with self.assertRaises(ValueError):
            collector.validate_response(data, "diagnostics")

    def test_both_endpoints_real_http(self):
        row = collector.collect_sample(self.opener, self.host)
        self.assertEqual(row["request_ok"], 1)
        self.assertEqual(row["state_LIGHT_A"], 19)
        self.assertEqual(row["diag_uptime_seconds"], 1234)
        self.assertGreater(float(row["timestamp_epoch"]), 0)
        datetime.strptime(row["timestamp_local"], "%Y-%m-%d %H:%M:%S")

    def test_partial_failures_in_both_directions_and_recovery(self):
        for failed, other in (("state", "diag"), ("diagnostics", "state")):
            with self.subTest(failed=failed):
                original = self.server.responses[f"/api/{failed}"]
                self.server.responses[f"/api/{failed}"] = (503, b"unavailable")
                row = collector.collect_sample(self.opener, self.host)
                self.assertEqual(row["request_ok"], 0)
                self.assertEqual(row[failed + "_request_ok"], 0)
                self.assertTrue(any(key.startswith(other + "_") and not key.endswith("request_ok") for key in row))
                self.assertIn("HTTP 503", row["collector_error"])
                self.server.responses[f"/api/{failed}"] = original
                self.assertEqual(collector.collect_sample(self.opener, self.host)["request_ok"], 1)

    def test_malformed_truncated_duplicate_and_oversized_json(self):
        bodies = (b"not json", b"{}", b"[]", b'{"pump":1,"pump":0}',
                  b'{"temperature":NaN}', b"\xff", b"x" * (collector.MAX_RESPONSE_BYTES + 1))
        for body in bodies:
            with self.subTest(body_length=len(body)):
                self.server.responses["/api/state"] = (200, body)
                row = collector.collect_sample(self.opener, self.host)
                self.assertEqual(row["state_request_ok"], 0)
                self.assertEqual(row["diagnostics_request_ok"], 1)
                self.assertNotIn("state_pump", row)

    def test_timeout_refusal_and_interrupted_http(self):
        for error in (TimeoutError(), URLError(TimeoutError()), URLError(ConnectionRefusedError()),
                      IncompleteRead(b"truncated", 30)):
            with patch.object(self.opener, "open", side_effect=error):
                values, message = collector.fetch_endpoint(self.opener, self.host, "state")
                self.assertEqual(values, {})
                self.assertTrue(message)

    def test_filenames_are_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            now = datetime(2026, 11, 4, 3, 45)
            first, stream = collector.create_log(directory, now)
            with stream:
                stream.write("keep")
            second, stream = collector.create_log(directory, now)
            stream.close()
            self.assertEqual(first.name, "2026-11-04_03-45.csv")
            self.assertEqual(second.name, "2026-11-04_03-45_1.csv")
            self.assertEqual(first.read_text(), "keep")

    def test_csv_run_partial_failed_and_reboot(self):
        full = collector.collect_sample(self.opener, self.host)
        self.server.responses["/api/state"] = (503, b"unavailable")
        partial = collector.collect_sample(self.opener, self.host)
        self.server.responses["/api/diagnostics"] = (503, b"unavailable")
        failed = collector.collect_sample(self.opener, self.host)
        reboot = dict(full, diag_uptime_seconds=2)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()), \
                patch.object(collector, "collect_sample", side_effect=[full, partial, failed, reboot]), \
                patch.object(collector.time, "sleep") as sleep, \
                patch.object(collector.os, "fsync", wraps=collector.os.fsync) as fsync:
            self.assertEqual(collector.run(self.host, samples=4, directory=directory), 0)
            self.assertEqual(sleep.call_count, 3)
            self.assertGreaterEqual(fsync.call_count, 5)
            with next(Path(directory).glob("*.csv")).open(newline="", encoding="utf-8") as stream:
                reader = csv.DictReader(stream)
                self.assertEqual(reader.fieldnames, list(collector.CSV_FIELDS))
                rows = list(reader)
            self.assertEqual(len(rows), 4)
            self.assertEqual(rows[1]["state_pump"], "")
            self.assertEqual(rows[1]["diag_uptime_seconds"], "1234")
            self.assertEqual(rows[2]["diag_sensor_cycles"], "")
            self.assertEqual(rows[3]["diag_uptime_seconds"], "2")
            self.assertEqual(rows[3]["collector_full_success_samples"], "2")
            self.assertEqual(rows[3]["collector_partial_samples"], "1")
            self.assertEqual(rows[3]["collector_failed_samples"], "1")

    def test_ctrl_c_closes_file_and_prints_summary(self):
        output = io.StringIO()
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(output), \
                patch.object(collector.time, "sleep", side_effect=KeyboardInterrupt):
            self.assertEqual(collector.run(self.host, directory=directory), 0)
            path = next(Path(directory).glob("*.csv"))
            with path.open(newline="", encoding="utf-8") as stream:
                self.assertEqual(len(list(csv.DictReader(stream))), 1)
            path.unlink()  # Windows refuses this if the writer is still open.
        self.assertIn("Stopped by Ctrl+C", output.getvalue())
        self.assertIn("Samples: 1", output.getvalue())
        self.assertNotIn("Traceback", output.getvalue())

    def test_interrupt_during_request_preserves_prior_rows(self):
        row = collector.collect_sample(self.opener, self.host)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()), \
                patch.object(collector, "collect_sample", side_effect=[row, KeyboardInterrupt]), \
                patch.object(collector.time, "sleep"):
            self.assertEqual(collector.run(self.host, directory=directory), 0)
            with next(Path(directory).glob("*.csv")).open(newline="", encoding="utf-8") as stream:
                self.assertEqual(len(list(csv.DictReader(stream))), 1)

    def test_disk_failure_stops_clearly(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), patch.object(collector, "create_log", side_effect=PermissionError):
            self.assertEqual(collector.run(self.host, samples=1), 1)
        self.assertIn("local I/O error", output.getvalue())

    def test_slow_cycle_does_not_catch_up(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()), \
                patch.object(collector.time, "monotonic", side_effect=[0, 12, 22]), \
                patch.object(collector.time, "sleep") as sleep:
            self.assertEqual(collector.run(self.host, interval=10, samples=2, directory=directory), 0)
            sleep.assert_called_once_with(10)


if __name__ == "__main__":
    unittest.main()
