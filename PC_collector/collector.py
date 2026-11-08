"""Collect read-only ESP32 state and diagnostics into one CSV per run."""

import argparse
import csv
from dataclasses import dataclass, replace
from datetime import datetime
from http.client import HTTPException
import json
import math
import os
from pathlib import Path
import time
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, Request, build_opener

# ---------------------------------------------------------
# CONFIGURATION
# ---------------------------------------------------------
ESP32_HOST = "192.168.0.19"
POLL_INTERVAL_SECONDS = 10
HTTP_TIMEOUT_SECONDS = 3
LOG_DIRECTORY = Path(__file__).resolve().parent / "LOGS"
MAX_RESPONSE_BYTES = 65536

STATE_FIELDS = (
    "pump", "LIGHT_S", "LIGHT_A", "AUTO", "MOD", "temperature", "humidity",
    "soil_percent", "water_percent", "wifi_connected", "time_synced",
    "htu21_valid", "soil_valid", "ldr_valid", "water_valid",
    "htu21_error", "soil_error", "ldr_error", "water_error",
)
DIAGNOSTIC_FIELDS = (
    "uptime_seconds", "sensor_cycles", "htu21_errors", "soil_errors", "ldr_errors",
    "ultrasonic_errors", "ultrasonic_timeouts", "blynk_poll_ok", "blynk_poll_errors",
    "blynk_upload_ok", "blynk_upload_errors", "wifi_reconnects",
    "app_event_queue_overflows", "button_events", "pump_commands", "light_commands",
    "auto_commands", "light_ramp_ticks", "light_ramp_steps", "time_sync_events",
    "task_deadline_misses", "heap_free_bytes", "heap_min_free_bytes",
    "stack_control_bytes", "stack_input_bytes", "stack_sensor_bytes",
    "stack_display_bytes", "stack_network_bytes", "stack_time_bytes", "stack_ramp_bytes",
)
METADATA_FIELDS = (
    "timestamp_local", "timestamp_epoch", "timestamp_utc_offset", "request_ok",
    "state_request_ok", "diagnostics_request_ok", "collector_error",
    "collector_total_samples", "collector_full_success_samples",
    "collector_partial_samples", "collector_failed_samples",
)
CSV_FIELDS = METADATA_FIELDS + tuple("state_" + key for key in STATE_FIELDS) + tuple(
    "diag_" + key for key in DIAGNOSTIC_FIELDS
)


def flatten_json(data, prefix=""):
    """Flatten objects with underscores; index arrays and reject name collisions."""
    result = {}

    def visit(value, name):
        if isinstance(value, dict):
            for key, child in value.items():
                if not isinstance(key, str) or not key:
                    raise ValueError("invalid JSON field name")
                visit(child, f"{name}_{key}" if name else key)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, f"{name}_{index}")
        else:
            if not name or name in result:
                raise ValueError("flattened JSON field collision")
            if value is not None and not isinstance(value, (str, int, float, bool)):
                raise ValueError("unsupported JSON value")
            result[name] = value

    visit(data, prefix)
    return result


def validate_response(data, endpoint):
    if not isinstance(data, dict):
        raise ValueError("expected a JSON object")
    flat = flatten_json(data)
    fields = STATE_FIELDS if endpoint == "state" else DIAGNOSTIC_FIELDS
    missing = [name for name in fields if name not in flat]
    if missing:
        raise ValueError("missing expected fields: " + ", ".join(missing))
    values = {key: flat[key] for key in fields}
    for key, value in values.items():
        if endpoint == "diagnostics":
            valid = type(value) is int and 0 <= value <= 2**64 - 1
        elif key in ("temperature", "humidity"):
            valid = value is None or (
                type(value) in (int, float) and math.isfinite(value)
            )
        elif key in ("wifi_connected", "time_synced") or key.endswith("_valid"):
            valid = type(value) is bool
        else:
            maximum = 1 if key in ("pump", "AUTO") else 2 if key == "MOD" else 100
            valid = type(value) is int and (
                -(2**31) <= value < 2**31 if key.endswith("_error") else 0 <= value <= maximum
            )
        if not valid:
            raise ValueError("invalid value for " + key)
    return values


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("non-finite JSON number")


# ---------------------------------------------------------
# HTTP COLLECTION
# ---------------------------------------------------------
def fetch_endpoint(opener, host, endpoint):
    request = Request(f"http://{host}/api/{endpoint}", headers={"Accept": "application/json"})
    try:
        with opener.open(request, timeout=HTTP_TIMEOUT_SECONDS) as response:
            if response.status != 200:
                raise ValueError(f"HTTP {response.status}")
            body = response.read(MAX_RESPONSE_BYTES + 1)
            if len(body) > MAX_RESPONSE_BYTES:
                raise ValueError("response too large")
        data = json.loads(body.decode("utf-8"), object_pairs_hook=unique_object,
                          parse_constant=reject_constant)
        return validate_response(data, endpoint), ""
    except HTTPError as error:
        error.close()
        return {}, f"{endpoint}: HTTP {error.code}"
    except (TimeoutError, URLError, OSError, HTTPException) as error:
        reason = getattr(error, "reason", error)
        message = "timeout" if isinstance(reason, TimeoutError) else type(reason).__name__
        return {}, f"{endpoint}: {message}"
    except (ValueError, OverflowError, RecursionError) as error:
        # Do not dump arbitrary response bodies into the CSV or console.
        return {}, f"{endpoint}: invalid response ({type(error).__name__})"


def collect_sample(opener, host):
    now = datetime.now().astimezone()
    row = {
        "timestamp_local": now.strftime("%Y-%m-%d %H:%M:%S"),
        "timestamp_epoch": f"{now.timestamp():.3f}",
        "timestamp_utc_offset": now.strftime("%z"),
    }
    errors = []
    for endpoint, prefix in (("state", "state_"), ("diagnostics", "diag_")):
        values, error = fetch_endpoint(opener, host, endpoint)
        row[endpoint + "_request_ok"] = int(not error)
        if error:
            errors.append(error)
        else:
            row.update({prefix + key: value for key, value in values.items()})
    row["request_ok"] = int(not errors)
    row["collector_error"] = "; ".join(errors)
    return row


# ---------------------------------------------------------
# CSV AND RUN LIFECYCLE
# ---------------------------------------------------------
@dataclass
class RunCounts:
    total_samples: int = 0
    full_success_samples: int = 0
    partial_samples: int = 0
    failed_samples: int = 0

    def with_sample(self, row):
        counts = replace(self, total_samples=self.total_samples + 1)
        successes = row["state_request_ok"] + row["diagnostics_request_ok"]
        if successes == 2:
            counts.full_success_samples += 1
        elif successes == 1:
            counts.partial_samples += 1
        else:
            counts.failed_samples += 1
        return counts

    def csv_values(self):
        return {"collector_" + key: value for key, value in vars(self).items()}


def create_log(directory, started):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    stem = started.strftime("%Y-%m-%d_%H-%M")
    suffix = 0
    while True:
        path = directory / f"{stem}{'_' + str(suffix) if suffix else ''}.csv"
        try:
            return path, path.open("x", encoding="utf-8", newline="")
        except FileExistsError:
            suffix += 1


def flush_log(stream):
    stream.flush()
    os.fsync(stream.fileno())


def print_status(row):
    clock = row["timestamp_local"][-8:]
    if row["request_ok"]:
        print(f"[{clock}] OK | uptime={row['diag_uptime_seconds']}s | "
              f"heap={row['diag_heap_free_bytes']} | "
              f"queue_overflow={row['diag_app_event_queue_overflows']}", flush=True)
    else:
        print(f"[{clock}] ERROR | {row['collector_error']}", flush=True)


def run(host=ESP32_HOST, interval=POLL_INTERVAL_SECONDS, samples=None, directory=LOG_DIRECTORY):
    if not math.isfinite(interval) or interval <= 0:
        raise ValueError("poll interval must be positive and finite")
    if samples is not None and samples <= 0:
        raise ValueError("sample count must be positive")
    started = datetime.now().astimezone()
    counts = RunCounts()
    path = None
    stream = None
    exit_code = 0
    # Reach the local device directly, independent of system HTTP proxy settings.
    opener = build_opener(ProxyHandler({}))
    try:
        path, stream = create_log(directory, started)
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        writer.writeheader()
        flush_log(stream)
        print(f"ESP32: {host} | interval: {interval:g}s | CSV: {path}", flush=True)
        while samples is None or counts.total_samples < samples:
            cycle_start = time.monotonic()
            row = collect_sample(opener, host)
            next_counts = counts.with_sample(row)
            row.update(next_counts.csv_values())
            writer.writerow(row)
            flush_log(stream)
            counts = next_counts
            print_status(row)
            if samples is not None and counts.total_samples >= samples:
                break
            remaining = interval - (time.monotonic() - cycle_start)
            # Skip catch-up bursts if requests took longer than the interval.
            time.sleep(remaining if remaining > 0 else interval)
    except KeyboardInterrupt:
        print("\nStopped by Ctrl+C.", flush=True)
    except OSError as error:
        exit_code = 1
        print(f"Collector stopped: local I/O error ({type(error).__name__}).", flush=True)
    finally:
        if stream is not None:
            try:
                flush_log(stream)
            except OSError:
                exit_code = 1
                print("Could not finish flushing the CSV.", flush=True)
            finally:
                try:
                    stream.close()
                except OSError:
                    exit_code = 1
                    print("Could not close the CSV.", flush=True)
        ended = datetime.now().astimezone()
        print(f"Start: {started:%Y-%m-%d %H:%M:%S %z}\n"
              f"End: {ended:%Y-%m-%d %H:%M:%S %z}\n"
              f"Samples: {counts.total_samples} | full success: {counts.full_success_samples} | "
              f"partial: {counts.partial_samples} | failed: {counts.failed_samples}\n"
              f"CSV: {path if path else 'not created'}", flush=True)
    return exit_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=ESP32_HOST, help="ESP32 IP or hostname (optional :port)")
    parser.add_argument("--interval", type=float, default=POLL_INTERVAL_SECONDS, help="poll interval in seconds")
    parser.add_argument("--samples", type=int, help="stop after this many samples; default: run until Ctrl+C")
    args = parser.parse_args()
    if not args.host or any(char in args.host for char in "/?#@\\ \t\r\n"):
        parser.error("host must be an IP or hostname, optionally with :port, without a URL scheme")
    if not math.isfinite(args.interval) or args.interval <= 0:
        parser.error("interval must be positive and finite")
    if args.samples is not None and args.samples <= 0:
        parser.error("samples must be positive")
    return run(args.host, args.interval, args.samples)


if __name__ == "__main__":
    raise SystemExit(main())
