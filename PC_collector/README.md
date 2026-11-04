# PC endurance collector

Requires Python 3.10+ on Windows 11. Only the standard library is used; no pip
dependencies. Firmware is unchanged. Run in a VS Code or Windows terminal:

```powershell
cd PC_collector
python collector.py
```

Default device: `192.168.0.19`; interval: 10 seconds; HTTP socket timeout: 3 seconds
per endpoint. Change the constants near the top of collector.py, or use:

```powershell
python collector.py --host 192.168.0.19 --interval 10
python collector.py --samples 2
```

The default run continues until **Ctrl+C**. The collector flushes/closes its CSV
and prints start/end times, total/full-success/partial/failed sample counts and
the file path. An interrupted in-flight sample is not written; earlier rows stay
saved. No analysis, graphs, database, or firmware configuration changes occur.

## Output

Each run creates a new `LOGS/YYYY-MM-DD_HH-MM.csv` relative to the script, even
when launched from another directory. Same-minute collisions receive `_1`, `_2`,
etc.; exclusive file creation prevents overwriting. Generated CSVs are Git-ignored.

One header is written immediately. Each row includes local time, epoch seconds,
UTC offset, success flags, compact errors, cumulative `collector_*` counts,
`state_*` application fields and `diag_*` runtime fields. PC local time comes from
the operating system: set Windows time zone to **Kyiv** before starting. The UTC
offset and epoch distinguish repeated local times during DST changes. Poll timing
uses a monotonic clock and does not depend on wall-clock adjustments.

The fixed header matches the current Stage 9 JSON schema in
`src/diagnostics/diagnostics_json.c` and `diagnostics.h`, including validity/error
and stack fields. Nested objects flatten with underscores, arrays with numeric
indices; ambiguous flattened names are rejected. Additional unknown firmware
fields are ignored to keep the header stable; update the field lists if the
firmware schema is intentionally extended. Missing expected fields or invalid
types fail that endpoint. No response bodies are dumped to the console or CSV.

Both endpoints are requested independently on every cycle. A failed endpoint
leaves its fields empty; the other endpoint's data is retained. `request_ok=1`
means both succeeded. An available JSON null also becomes an empty CSV cell;
the endpoint status and sensor validity/error columns distinguish unavailable
readings from request failures. Boolean state fields use Python CSV `True/False`;
numeric flags use 0/1. Large integer counters retain their full decimal precision.

The two requests are sequential, so their snapshots are not atomic across the
device; a reboot or state change can occur between them. Counters/uptime are kept
raw, even if they decrease after reboot. No previous sample is substituted for
missing data. Response bodies are limited to 64 KiB. The timeout is a socket
operation timeout, not a guaranteed total duration for a slowly streaming server.
System HTTP proxies are bypassed for direct local-device access.

The header and every row are flushed and `os.fsync()` is called. This limits
buffered data loss; it cannot guarantee hardware survival of every power failure.
Local disk errors stop the run with a short message rather than silently discard
data. HTTP/JSON failures produce rows and continue at the normal interval, without
immediate retries. Slow cycles wait one full interval rather than catch up in a
burst. Console output is one compact status line per sample.

## Validation and a long run

```powershell
python -m py_compile collector.py test_collector.py
python -m unittest discover -s . -p "test_collector.py" -v
```

Tests use a local HTTP fixture server, temporary CSV files and simulated faults:
valid/invalid JSON, partial failure in either direction, recovery, timeout,
connection refusal, truncated HTTP, rebooted uptime, filename collisions, counter
totals, flushing, Ctrl+C, disk failure and slow cycles. They do not establish
48–72 hour endurance. Before a long run, try `--samples 2`, inspect both endpoint
flags, ensure Windows is set to Kyiv time and prevent sleep/hibernation. Then start
the default collector and leave it running for the intended duration. Stop with
Ctrl+C and retain the CSV for later analysis.
