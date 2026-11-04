"""Validate a bounded CSV capture, select its longest continuous run, and plot it."""

import argparse
import csv
from datetime import datetime
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "PC_collector"))
from collector import STATE_FIELDS, DIAGNOSTIC_FIELDS, validate_response

COUNTERS = [key for key in DIAGNOSTIC_FIELDS if key != "uptime_seconds"
            and not key.startswith(("heap_", "stack_"))]
MAX_GAP_SECONDS = 25


def read_capture(path, max_rows=None):
    raw = path.read_bytes()
    # The collector writes one physical line per row; ignore an unfinished last write.
    lines = raw.splitlines(keepends=True)
    if lines and not lines[-1].endswith((b"\n", b"\r")):
        lines.pop()
    if max_rows is not None:
        lines = lines[:max_rows + 1]
    captured = b"".join(lines)
    reader = csv.DictReader(io.StringIO(captured.decode("utf-8-sig")))
    required = {"timestamp_local", "timestamp_epoch", "timestamp_utc_offset",
                "request_ok", "state_request_ok", "diagnostics_request_ok"}
    if not required.issubset(reader.fieldnames or []):
        raise ValueError(f"{path.name}: missing timestamp/status columns")
    parsed = []
    counts = dict(full=0, partial=0, failed=0, invalid=0, time_order_errors=0,
                  timestamp_mismatches=0, observed_uptime_resets=0)
    previous_epoch = None
    previous_uptime = None
    for number, row in enumerate(reader, 1):
        record = {"row": number, "valid": False}
        try:
            if None in row or any(value is None for value in row.values()):
                raise ValueError("incomplete row")
            epoch = float(row["timestamp_epoch"])
            if not math.isfinite(epoch):
                raise ValueError("invalid timestamp")
            local = datetime.strptime(row["timestamp_local"] + row["timestamp_utc_offset"],
                                      "%Y-%m-%d %H:%M:%S%z")
            record.update(epoch=epoch, local=local.isoformat(sep=" "))
            if abs(epoch - local.timestamp()) >= 1.01:
                counts["timestamp_mismatches"] += 1
                raise ValueError("timestamp mismatch")
            ordered = previous_epoch is None or epoch > previous_epoch
            if not ordered:
                counts["time_order_errors"] += 1
            previous_epoch = epoch
            flags = [int(row[key]) for key in ("state_request_ok", "diagnostics_request_ok")]
            if any(flag not in (0, 1) for flag in flags) or int(row["request_ok"]) != int(all(flags)):
                raise ValueError("invalid status flags")
            counts[{0: "failed", 1: "partial", 2: "full"}[sum(flags)]] += 1
            for endpoint, prefix, fields, ok in (
                ("state", "state_", STATE_FIELDS, flags[0]),
                ("diagnostics", "diag_", DIAGNOSTIC_FIELDS, flags[1]),
            ):
                if not ok:
                    continue
                values = {}
                for key in fields:
                    text = row[prefix + key]
                    if endpoint == "state" and (key.endswith("_valid") or key in ("wifi_connected", "time_synced")):
                        if text not in ("True", "False"):
                            raise ValueError("invalid boolean")
                        value = text == "True"
                    elif endpoint == "state" and key in ("temperature", "humidity"):
                        value = float(text) if text else None
                    else:
                        value = int(text)
                    values[key] = value
                record[endpoint] = validate_response(values, endpoint)
            if flags[1]:
                uptime = record["diagnostics"]["uptime_seconds"]
                if previous_uptime is not None and uptime < previous_uptime:
                    counts["observed_uptime_resets"] += 1
                previous_uptime = uptime
            record["valid"] = all(flags) and ordered
        except (ValueError, KeyError, OverflowError):
            counts["invalid"] += 1
        parsed.append(record)
    return parsed, dict(file=path.name, captured_rows=len(parsed), sha256=captured and hashlib.sha256(captured).hexdigest(),
                        **counts)


def continuous_runs(rows):
    runs, current = [], []
    for row in rows:
        split = not row["valid"]
        if current and row["valid"]:
            previous = current[-1]
            gap = row["epoch"] - previous["epoch"]
            split |= not 0 < gap <= MAX_GAP_SECONDS
            split |= any(row["diagnostics"][key] < previous["diagnostics"][key]
                         for key in ["uptime_seconds"] + COUNTERS)
        if split and current:
            runs.append(current)
            current = []
        if row["valid"]:
            current.append(row)
    if current:
        runs.append(current)
    return runs


def build_summary(rows, inventory, selected):
    gaps = [b["epoch"] - a["epoch"] for a, b in zip(selected, selected[1:])]
    d = [row["diagnostics"] for row in selected]
    summary = dict(
        source=inventory, selection=dict(first_row=selected[0]["row"], last_row=selected[-1]["row"],
        samples=len(selected), start=selected[0]["local"], end=selected[-1]["local"],
        duration_seconds=round(selected[-1]["epoch"] - selected[0]["epoch"], 3),
        interval_median_seconds=round(statistics.median(gaps), 3) if gaps else None,
        interval_min_seconds=round(min(gaps), 3) if gaps else None,
        interval_max_seconds=round(max(gaps), 3) if gaps else None,
        gap_limit_seconds=MAX_GAP_SECONDS),
        counter_deltas={key: dict(start=d[0][key], end=d[-1][key], delta=d[-1][key] - d[0][key]) for key in COUNTERS},
        uptime_start=d[0]["uptime_seconds"], uptime_end=d[-1]["uptime_seconds"],
        heap_free_min=min(x["heap_free_bytes"] for x in d),
        heap_min_reported=min(x["heap_min_free_bytes"] for x in d),
        stack_min_bytes={key: min(x[key] for x in d) for key in DIAGNOSTIC_FIELDS if key.startswith("stack_")},
    )
    available = [row for row in rows if "epoch" in row]
    summary["source_span"] = dict(start=available[0]["local"], end=available[-1]["local"],
                                  duration_seconds=round(available[-1]["epoch"] - available[0]["epoch"], 3))
    summary["series"] = {}
    for key in ("LIGHT_S", "LIGHT_A", "temperature", "humidity"):
        values = [fresh_value(row["state"], key) for row in selected]
        finite = [v for v in values if math.isfinite(v)]
        summary["series"][key] = dict(valid_samples=len(finite), missing_samples=len(values) - len(finite),
                                      minimum=min(finite) if finite else None, maximum=max(finite) if finite else None)
    return summary


def fresh_value(state, key):
    sensor = "ldr" if key == "LIGHT_A" else "htu21" if key in ("temperature", "humidity") else None
    if sensor and (not state[sensor + "_valid"] or state[sensor + "_error"] != 0):
        return math.nan
    value = state[key]
    return float(value) if value is not None else math.nan


def plot(selected, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "axes.titlesize": 15, "figure.facecolor": "white", "path.simplify": False})
    zone = datetime.fromisoformat(selected[0]["local"]).tzinfo
    times = [datetime.fromtimestamp(row["epoch"], zone) for row in selected]
    offset = times[0].strftime("%z")
    axis_label = f"Местное время, {times[0]:%d.%m.%Y} (UTC{offset[:3]}:{offset[3:]})"
    def format_axis(ax):
        ax.grid(alpha=0.18)
        ax.xaxis.set_major_locator(mdates.HourLocator(interval=2, tz=zone))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M", tz=zone))
        ax.set_xlim(times[0], times[-1])
    fig, ax = plt.subplots(figsize=(11.5, 4.7), layout="constrained")
    for key, label, color in (("LIGHT_S", "LIGHT_S — заданный уровень", "#145da0"),
                              ("LIGHT_A", "LIGHT_A — показание LDR", "#d97706")):
        ax.plot(times, [fresh_value(row["state"], key) for row in selected], label=label,
                color=color, linewidth=1.5, drawstyle="steps-post" if key == "LIGHT_S" else "default")
    ax.set(title="Заданный и измеренный уровень освещения", ylabel="Уровень, %", ylim=(-2, 102),
           xlabel=axis_label)
    format_axis(ax)
    ax.legend(loc="upper right", frameon=False)
    fig.savefig(output / "test-light-telemetry.png", dpi=180, metadata={"Software": "GROW SYSTEM documentation"})
    plt.close(fig)
    fig, axes = plt.subplots(2, 1, figsize=(11.5, 6.5), sharex=True, layout="constrained")
    for ax, key, label, color in zip(axes, ("temperature", "humidity"),
                                    ("Температура, °C", "Относительная влажность, %"), ("#b93838", "#087e8b")):
        ax.plot(times, [fresh_value(row["state"], key) for row in selected], color=color, linewidth=1.3)
        ax.set_ylabel(label)
        format_axis(ax)
    axes[0].set_title("Температура и относительная влажность")
    axes[1].set_xlabel(axis_label)
    fig.savefig(output / "test-climate-telemetry.png", dpi=180, metadata={"Software": "GROW SYSTEM documentation"})
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--max-rows", type=int, help="pin the analyzed prefix of an actively growing log")
    parser.add_argument("--output", type=Path, default=ROOT / "docs" / "assets")
    args = parser.parse_args()
    if args.max_rows is not None and args.max_rows < 1:
        parser.error("--max-rows must be positive")
    rows, inventory = read_capture(args.source, args.max_rows)
    runs = [run for run in continuous_runs(rows) if len(run) >= 2]
    if not runs:
        raise SystemExit("No continuous valid interval with at least two samples")
    selected = max(runs, key=lambda run: (run[-1]["epoch"] - run[0]["epoch"], len(run)))
    summary = build_summary(rows, inventory, selected)
    args.output.mkdir(parents=True, exist_ok=True)
    plot(selected, args.output)
    (args.output / "preliminary-test-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
