# Stage 9 runtime diagnostics

Diagnostics observe the existing tasks and do not own application state. All
counters live in RAM and reset on reboot. There is no diagnostics task, collector,
flash log or NVS checkpoint. The ESP-IDF HTTP server owns its framework task.

## Local API

Connect to the same reachable network as the device. GET
`http://ESP32_IP/api/state` and `http://ESP32_IP/api/diagnostics` return JSON with
`Cache-Control: no-store`. There are no write endpoints, authentication, cloud
uploads of diagnostics or periodic UART JSON. Use a trusted local network.
The listener uses port 80 and binds available interfaces; it is not advertised
through a discovery service. No passwords, tokens, SSIDs or Blynk URLs are part
of either schema.

The server starts only after successful network-service initialization. Failure
is logged but does not prevent Blynk task startup or local controls. It remains
running across Wi-Fi disconnects and becomes reachable when connectivity returns.
Startup failure is not retried until reboot. Server task priority is 2, stack
4096 bytes, maximum client sockets 2, receive/send timeouts 2 seconds. Slow clients
can delay other diagnostic requests, but handlers never execute in control_task.
GET handlers copy snapshots and serialize them without reading sensor hardware.

### `/api/state` schema

| Fields | JSON type and meaning |
| --- | --- |
| `pump`, `AUTO` | Integer 0 or 1 |
| `LIGHT_S`, `LIGHT_A` | Integer percent; commanded output and measured LDR |
| `MOD` | Integer 0 IDLE, 1 DAWN, 2 DUSK |
| `temperature`, `humidity` | Number in Celsius/percent; null before valid finite readings |
| `soil_percent`, `water_percent` | Integer percent; interpret alongside validity flags |
| `wifi_connected`, `time_synced` | Boolean application state |
| `htu21_valid`, `soil_valid`, `ldr_valid`, `water_valid` | Boolean: at least one good reading has been received |
| `htu21_error`, `soil_error`, `ldr_error`, `water_error` | Integer ESP-IDF status of the latest acquisition; 0 means OK |

Last-good readings remain visible after a failure, with the current error code.
An invalid percentage field can initially be zero; its validity flag distinguishes
that placeholder from a measured zero. The response is one coherent app_state copy.

### `/api/diagnostics` schema and counter definitions

All fields are JSON integers. Cumulative fields use uint64_t and serialize directly
as decimal integers, without a floating-point conversion. Consumers requiring
exact large integers should use an integer-aware parser (Python supports these).

| Fields | Meaning |
| --- | --- |
| `uptime_seconds` | `esp_timer_get_time() / 1000000`; monotonic boot uptime, independent of SNTP |
| `sensor_cycles` | Started acquisition cycles; not successful measurements |
| `htu21_errors`, `soil_errors`, `ldr_errors` | Failed fresh driver reads, once per failure |
| `ultrasonic_errors` | Failed or invalid HC-SR04 measurements; includes timeouts |
| `ultrasonic_timeouts` | Subset of ultrasonic_errors with ESP_ERR_TIMEOUT; do not add the two together |
| `blynk_poll_ok`, `blynk_poll_errors` | Completed poll results, including HTTP and parse validation |
| `blynk_upload_ok`, `blynk_upload_errors` | Completed upload results; intentionally skipped uploads are not errors |
| `wifi_reconnects` | Calls to esp_wifi_connect while disconnected, including the initial attempt and immediate errors; not successful connections |
| `app_event_queue_overflows` | xQueueSend failure after its allowed wait, for every producer; invalid arguments/uninitialized queue do not count |
| `button_events` | Debounced press events before queue submission, including presses subsequently dropped |
| `pump_commands`, `light_commands`, `auto_commands` | Commands dispatched by control_task from buttons or Blynk, including unchanged values, limits and driver failures; no telemetry or ramp commands |
| `light_ramp_ticks` | Ramp events processed by control_task, including IDLE/AUTO OFF ticks |
| `light_ramp_steps` | Ramp ticks that change brightness and successfully apply the PWM driver; rolled-back failures do not count |
| `time_sync_events` | Observed application transitions from unsynchronized to synchronized; duplicate notifications do not count |
| `task_deadline_misses` | Major inter-iteration delays for sensor/display/time/ramp tasks |
| `heap_free_bytes` | Current esp_get_free_heap_size() |
| `heap_min_free_bytes` | esp_get_minimum_free_heap_size() since boot |
| `stack_control_bytes`, `stack_input_bytes`, `stack_sensor_bytes`, `stack_display_bytes`, `stack_network_bytes`, `stack_time_bytes`, `stack_ramp_bytes` | Cached task stack high-water marks: minimum remaining free stack, in bytes in this ESP-IDF version |

Counter increments and the counter/stack snapshot copy use a short FreeRTOS
critical section. Heap and monotonic time are sampled outside it, so the full
response is an observation over a short interval, not a global atomic transaction.
No allocator, network call, logging or stack scan runs inside that critical section.

Each task samples its own stack at startup and eligible iterations, no more than
once per 10 seconds. This avoids retaining handles for tasks that can be deleted
(notably OLED failure and input initialization failure). Sleeping tasks refresh
less often: time about every minute, ramp every ten minutes, input upon activity.
Zero means not sampled or no remaining free stack; investigate it. A stopped task
retains its last sample: these metrics are not a task-liveness monitor. The HTTP
snapshot never dereferences potentially deleted task handles.

Deadline detection compares successive iteration timestamps with
`period + max(250 ms, 10% of period)`, using monotonic microseconds. One excessive
gap adds one miss, not the number of skipped periods. Sensor/display begin after
their startup setup; time/ramp include their first scheduled wake. Checks do not
change existing delays or scheduling. A terminated task cannot report a later
miss. Network deadlines are deliberately excluded: offline periods and the
existing poll-before-upload gating would otherwise look like scheduling faults.
Network outcomes are observed through the dedicated attempt/result counters.

## Application partition

The first Stage 9 link exceeded the old 1 MiB app partition by 13,160 bytes.
With explicit user approval, platformio.ini now selects partitions.csv with:

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | 0x9000 | 0x6000 (unchanged) |
| PHY | 0xf000 | 0x1000 (unchanged) |
| Factory app | 0x10000 (unchanged) | 0x180000 (1.5 MiB) |

The table ends at 0x190000, within 2 MiB. The existing 16 MB versus 2 MB Flash
configuration mismatch is not fixed. Standard PlatformIO upload must include
the generated partition table as well as firmware; uploading only the new app
against the old table is insufficient. No erase-all operation is required or
performed. Source sdkconfig and Flash/PSRAM flags are unchanged.

## Validation and endurance checklist

Host tests cover concurrent increments, independent snapshots, exact integer JSON
formatting, malformed-size rejection, null sensor values, deadline tolerances,
stack sampling throttling, and the production control handler's queue overflow,
command counts, duplicate synchronization, ramp ticks, successful steps and PWM
rollback. They use Windows lock/RTOS stubs, not an ESP32 scheduler or network.
Build and static checks do not establish hardware endurance or real HTTP service.

- A: Upload firmware and partition table; boot with Wi-Fi, open both endpoints,
  parse JSON, and confirm no credentials appear. Check unsupported write methods
  cannot change state. Repeat after Wi-Fi reconnection.
- B: Use physical pump/light/AUTO controls and Blynk V3/V5/V7; compare state JSON,
  OLED and physical outputs. Confirm 500 ms OLED refresh and unchanged V0..V8 map.
- C: Observe sensor_cycles advancing about every two seconds, queue overflow
  remaining zero normally, and commands increasing once per handled command.
  With AUTO OFF/IDLE, a ten-minute tick raises ticks but not steps; an active
  successful ramp raises both. Validate monotonic uptime and plausible heap/stack.
- D: Safely disconnect one sensor at a time; check only corresponding acquisition
  errors increase and stale/invalid indicators match. For HC-SR04, distinguish
  missing echo timeouts from invalid measurements. Reconnect and confirm recovery.
- E: Disable Wi-Fi and restore it; attempts must increase while local controls,
  sensors, OLED and FSM remain operational. Temporarily make Blynk unavailable;
  poll/upload failure counters should reflect actual attempted operations.
- F: Poll both endpoints every 10–30 seconds for 48–72 hours; verify uptime never
  resets unexpectedly, free heap stabilizes after warmup, minimum heap/stack retain
  adequate margin, normal queue overflows stay zero and deadline misses do not
  accumulate unexplained. Heap minimum may fall during initial TLS/HTTP warmup.
  Compare runs with and without polling, and record reboot/network/sensor incidents.
- Stress: Use both clients, rapid physical/remote commands and slow/aborted HTTP
  requests together; verify bounded server resources and continued local operation.
  Queue saturation and initialization-failure branches are host-tested; physical
  stress may not naturally reach them. Do not change production queue/timing merely
  to force faults. No 48–72 hour run has been performed as part of this code change.

No PC collector or persistent diagnostic storage is implemented.
