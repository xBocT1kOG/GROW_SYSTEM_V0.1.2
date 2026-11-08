# Firmware V0.2 skeleton

## Current status: Stage 8 Wi-Fi and Blynk networking

`network_service` owns station Wi-Fi, SNTP and bounded HTTPS requests;
`network_task` publishes connection/time events, polls controls and uploads full
application snapshots. Only control_task applies remote commands to drivers.
The old Wi-Fi startup and main-loop Blynk path are inactive; legacy functions
remain as reference. Local controls, sensors, display and FSM run independently.

Wi-Fi connects/reconnects on a five-second retry interval. SNTP uses pool.ntp.org
and the existing EET/EEST timezone rule. Time is reported synchronized only after
the time service confirms valid synchronized time. Initialization failures log
and leave local operation running; they require reboot to retry initialization.
Connection/request failures retry normally without reboot or busy loops.

| Pin | Value | Direction |
| --- | --- | --- |
| V0 | TEMP, Celsius | Telemetry |
| V1 | HUM, percent | Telemetry |
| V2 | SOIL, percent | Telemetry |
| V3 | PUMP, 0/1 | Control and state |
| V4 | WATER, percent | Telemetry |
| V5 | LIGHT_S, 0..100 percent | Control and state |
| V6 | LIGHT_A/LDR, 0..100 percent | Telemetry |
| V7 | AUTO, 0/1 | Control and state |
| V8 | MOD, 0 IDLE / 1 DAWN / 2 DUSK | Telemetry only |

Independent `BLYNK_CONTROL_POLL_MS` and `BLYNK_TELEMETRY_PERIOD_MS` timers each
default to 5000 ms. GET `/external/api/get` reads only V3/V5/V7; GET
`/external/api/batch/update` sends a coherent snapshot without sensor reads.
Unavailable or currently failed sensor channels are omitted, leaving their last
cloud value until acquisition recovers. V6 now has its assigned LDR mapping.
See the official [multi-value read](https://docs.blynk.io/en/blynk.cloud/device-https-api/get-multiple-datastream-values)
and [batch update](https://docs.blynk.io/en/blynk.cloud/device-https-api/update-multiple-datastreams-api) contracts.

The parser accepts integer numbers/strings/single-element arrays, rejects
malformed or missing controls, validates booleans and clamps V5 to 0..100.
Last-observed values suppress repeated commands; successful state uploads update
that baseline so device echoes do not become commands. At first connection,
remote controls differing from startup state are applied. On reconnect, retained
baselines preserve offline local changes unless a remote value also changed.
Failed polling or queue submission defers uploads until a successful poll;
reconnection also requires a fresh poll. This prevents knowingly overwriting
unread controls. REST has no revision/atomic compare-and-set: a cloud edit between
poll and upload can still lose to the later write, and a same-value cloud write
cannot be distinguished from an unchanged value.

Remote pump/light/AUTO use the same control helpers as buttons. AUTO OFF makes
MOD IDLE without changing brightness; ON waits for the next scheduled trigger.
Remote brightness preserves active ramps, except the existing DAWN target
completion rule. MOD is never accepted as a remote command.

HTTPS verifies certificates, disables redirects, sets a 2000 ms network timeout
and bounds responses to 511 bytes. Requests run only in the network task
(priority 3, 8192-byte stack). Failures log error names without request URLs,
credentials or response bodies. Existing credential storage remains unchanged.
The configured cloud host remains blynk.cloud; regional routing must be verified
on the actual device. Network initialization does not erase NVS on failure.

Firmware builds successfully. Protocol/baseline host tests cover malformed data,
integer bounds, clamping, ignored MOD and echo suppression. Hardware/network task
timing and real cloud behavior remain unverified. Flash configuration is unchanged;
the 16 MB / 2 MB mismatch warning remains. The app occupies about 99% of its current
1 MiB partition, leaving little space for later stages.

### Stage 8 hardware and Blynk checklist

- A: Verify all V0..V8 values against local state; vary sensors, including LDR.
  Check invalid readings are not uploaded as zero and resume after recovery.
- B: Change V3 between 0/1; verify pump hardware, snapshot and cloud agree.
- C: Set V5 to 0, 30, 50 and 100; verify PWM and OLED L-S. During DUSK,
  set 30 to 50 and verify the next ramp is 45. During DAWN, set at/above target
  and verify completion without lowering brightness. Hold V5 unchanged across
  several polls: no repeated remote commands or interference with ramps.
- D: Change V7; verify OLED AUTO. OFF cancels an active cycle without changing
  brightness; ON does not start a missed cycle and waits for the next trigger.
- E: Verify V8 reports 0/1/2 for IDLE/DAWN/DUSK. Writing V8 must not control MOD.
- F: Use physical pump/light/AUTO buttons; verify the next successful upload
  publishes their state and subsequent polls do not undo it.
- G: Disconnect Wi-Fi; verify buttons, pump, sensors, OLED and FSM keep working.
  Reconnect; verify automatic recovery, fresh polling, telemetry and SNTP as
  needed. Test unavailable Blynk, bad authentication, invalid/missing datastreams
  and cold boot without network: bounded retries, no reset or secret logging.
  Verify first-connection remote state, offline local changes, clock/DST and
  scheduled 06:00/20:00 behavior on the board.

## Historical Stage 7 OLED application snapshots

The only active screen owner is `tasks/display_task.c`, started once after the
existing shared I2C bus and OLED device setup. The old main.c display task is
removed; its source remains in Git history. There is no second bus, OLED device,
display task, or initialization sequence.

`SSD1306_CustomLib.h` retains every original register, glyph, addressing,
initialization, clear, and print sequence. Its only source edits change the two
global bus/device definitions to `extern`; main.c still owns their definitions.
The new task preserves the startup banner/delays, then refreshes every
`DISPLAY_PERIOD_MS` (2000 ms) using `vTaskDelayUntil`.

The original cells use pages 0/2/4/6 and columns 1/63, with unchanged 5+1 pixel
characters. Each field is padded to ten characters (60 pixels), so smaller
numbers and ON/OFF transitions erase old suffixes without a frame-wide clear.
The eight fields are TEMP, HUM, SOIL, WATER, LIGHT_S, LIGHT_A, AUTO, and MOD.
Physical light labels are `L-S` and `L-A`: full labels at 100% exceed the right
column width, and the original font has no underscore glyph. No font or layout
redesign is introduced. Invalid sensors show N/A; previously valid stale data
continues to follow Stage 6's retention policy.

`app_state_get_snapshot()` returns a full coherent copy. Control_task publishes
after initialization and after every event, using the existing short critical
section expanded to cover `app_state_t`. The display copies once per frame,
then renders without holding a lock. Only control_task writes published state.
The sensor-only getter remains for legacy telemetry.

TEMP/HUM/SOIL/WATER come from `snapshot.sensors`; LIGHT_S from
`snapshot.light.system_level_percent`, LIGHT_A from `snapshot.light.actual_percent`
(with the LDR validity flag), AUTO from `auto_enabled`, and MOD from the enum
mapped locally to IDLE/DAWN/DUSK. No sensor reads, actuator calls, or application
state writes occur in the display task. FSM, calibration, and Blynk are unchanged.

A local transport wrapper around the custom library's existing transmit calls
uses a 100 ms timeout and latches the first error. It preserves transmitted bytes
and skips further writes after failure. An init/render failure logs once and
terminates only the display task, without resetting or reinitializing the OLED.
Device-registration/task-creation errors are logged without aborting main.
There is no automatic display retry in this stage. The display priority is 3,
below sensors and control. ESP-IDF still serializes shared I2C transactions;
no additional I2C mutex is added. An electrical fault that physically holds the
shared bus can also disrupt HTU21 readings, which retain their bounded-error path.

Host tests use the real formatter and custom library with a recording transport:
initial command bytes, clearing, page/column addressing, font compatibility,
cell width, field sources, invalid sensors, and trailing-space clearing pass.
These checks do not establish physical OLED or bus-fault behavior.

### Stage 7 hardware checklist

- A: Reboot repeatedly: verify the original startup screen, reliable OLED init,
  no corrupt/duplicate startup artifacts, and no wrap/overlap at 100% values.
- B: Change temperature, humidity, soil, and water; confirm all four displayed
  values update on the two-second refresh. Initial invalid readings show N/A.
- C: Press light +/-: L-S changes by 5%; L-A changes only with measured light.
  Check 100% to 95%/0% transitions leave no residual glyphs.
- D: Toggle AUTO: ON/OFF updates without leftover characters.
- E: Verify MOD reads IDLE, DAWN, and DUSK during the corresponding states.
- F: Run several minutes with button activity and simultaneous HTU21 reads:
  control stays responsive, sensors update, and OLED neither freezes nor
  produces new shared-bus errors.
- Failure isolation: with the display unavailable at boot or disconnected during
  refresh, check the single display error and continued buttons, pump, FSM, and
  sensor-task execution. A stopped display requires reboot after reconnecting.

The future dedicated Blynk LIGHT_A pin remains unassigned. Flash/partition
settings are unchanged; the existing 16 MB / 2 MB warning remains technical debt.

## Historical Stage 6 sensor acquisition

The user reports local control and basic automatic light behavior checked on
hardware. Stage 6 itself still requires board validation.

One `sensor_task` acquires HTU21, soil, LDR, and one HC-SR04 reading per cycle,
then publishes a complete `APP_EVT_SENSOR_UPDATE`. It uses the existing 2000 ms
period with `vTaskDelayUntil`, priority 4, and a 4096-byte stack. Only
`control_task` accepts the snapshot into application state and updates
`light.actual_percent`. Commanded brightness, the FSM, and AUTO are unaffected.

### Drivers and ownership

- `htu21_init(bus)` registers address 0x40 on the existing I2C bus;
  `htu21_read(&temperature, &humidity)` checks every transaction and CRC.
  No-hold commands retain the legacy sequence with 60 ms conversion waits and
  bounded 100 ms transaction timeouts. Outputs change only after both reads pass.
- `soil_init(adc)` and `ldr_init(adc)` configure separate channels on the same
  ADC1 handle created once during startup. `soil_read(&raw, &percent)` and
  `ldr_read(&raw, &percent)` retain raw samples and map 500..3800 to 0..100%,
  clamping outside that range. Both mappings increase with raw voltage.
- `ultrasonic_init()` owns TRIG/ECHO configuration;
  `ultrasonic_read(&distance)` emits one 10 us pulse with 20 ms rise and 30 ms
  fall timeouts. Already-high ECHO and distances outside (0, 400] cm are errors.
  The task has lower priority than control/input and does not mask interrupts;
  physical echo timing under load still needs board validation.

Only `sensor_task` calls sensor read APIs. Legacy sensor functions remain as
reference, but startup no longer registers a second HTU21 device and the legacy
main loop no longer reads sensors or runs the 20-sample ultrasonic filter.

OLED retains its device and renderer on the same I2C bus. The installed ESP-IDF
master driver serializes bus transactions internally, so no additional I2C mutex
is added. HTU21 conversion delays do not hold the bus; OLED can use its separate
device while HTU21 converts. Bus/device initialization completes before readers
start, and no second task accesses the HTU21 device.

### Filtering, validity, and compatibility

The sensor task owns a ring of the last five valid distances. It reports a
median immediately, including during warm-up; even counts use the mean of the
middle pair. Invalid samples do not alter history. Water distance 16 cm maps
to 0%, 7 cm to 100%, with a linear inverse mapping and endpoint clamps.

Snapshots retain the original temperature, humidity, raw ADC, percentage, and
distance fields. Four `*_valid` flags mean a good reading has been obtained at
least once. Four `*_error` fields hold each sensor's latest read result. Thus a
valid field with an error is retained stale data, not a fresh sample. On initial
failure, validity remains false; raw ADC is -1 and floating measurements are NaN
in the sensor task. Percentage fields must be interpreted only with validity.
No error-counter framework existed, so none is introduced. Logs report error
changes and recovery rather than repeated identical failures every two seconds.

`app_control_get_sensors()` copies the last accepted snapshot. A short ESP-IDF
critical section protects that copy across cores; it contains no I/O or waits.
Only control_task writes the published copy. Legacy OLED and HTTP telemetry
use this read-only compatibility API rather than touching hardware. OLED shows
N/A until the first good reading, then retains the last good display value.
Soil display uses the mapped percent. The existing Blynk raw-soil payload and
virtual pins are unchanged; its telemetry loop skips packets when a required
sensor has no valid reading or its latest read failed. Display/network modules
are not migrated. The future dedicated LIGHT_A Blynk pin remains unassigned.

### Stage 6 validation and hardware checklist

Host tests cover ADC calibration/monotonicity, water endpoints, partial median
history, outliers, invalid samples, and ring wrap. Existing FSM tests also pass.
These tests do not validate bus transactions, physical sensing, or scheduling.
The Stage 6 `esp32s3` firmware build passed (RAM 11.6%, application partition
91.0%). ELF inspection confirms the new sensor task/drivers are linked and
legacy polling/filter functions are absent. No new compiler warnings appeared;
the unused legacy `oled_fill` warning and the 16 MB / 2 MB Flash warning remain.
The old volatile-pointer warnings disappeared with the legacy acquisition calls.

Use the legacy OLED for temperature/humidity/soil/water, UART for errors, and a
debugger snapshot (`app_control_get_sensors()` or `published_sensors`) for raw
ADC, LDR, distance, validity, and last-error fields. No continuous sensor-value
logging or new diagnostics task is enabled.

- A: HTU21 temperature/humidity change plausibly without repeated I2C errors.
  Check initial disconnect, transient failure, and recovery; good values must
  remain unchanged on failed reads, with validity/error distinguishing the case.
- B: Soil dry/low readings approach 0%, wet/high readings approach 100%; inspect
  raw values and verify 500/3800 endpoints and clamping.
- C: Cover LDR: raw and percentage decrease; illuminate it: both increase.
  Verify PWM, target, and FSM do not react to LDR changes.
- D: Check plausible water distance, stabilization over up to five valid cycles,
  16 cm approximately 0% and 7 cm approximately 100%. Disconnect/stall ECHO:
  history must not receive invalid values; before any success it stays invalid,
  afterward the previous filtered result is retained with an error.
- E: Verify two-second acquisitions, responsive buttons and unchanged Light FSM
  during I2C errors and echo timeouts. Confirm the old roughly two-second
  blocking ultrasonic averaging is absent and OLED still works on the shared bus.

## Historical Stage 5 scheduled light control

The user confirmed Stage 4 local control on hardware. Stage 5 adds a pure
`app/light_fsm.c` FSM invoked only by `control_task`. Its context, application
state, and actuator decisions all belong to that task. LDR remains telemetry only.

After legacy Wi-Fi/time initialization, startup creates `time_task` and
`ramp_task`. Both use `vTaskDelayUntil`: time snapshots every 60 seconds and
ramp events every 600 seconds. No immediate ramp occurs at task startup or at
a schedule transition. The first ramp step uses the next global ramp tick,
so it can occur less than ten minutes after the scheduled transition.
Queue-full events are dropped with a warning rather than blocking producers.

`time_service` registers an SNTP callback before SNTP starts. An atomic service
flag records the first successful synchronization; snapshots use `localtime_r`
and the unchanged system timezone. Wi-Fi loss alone does not invalidate an
already synchronized running clock. Before synchronization, schedule checks
cannot start cycles, but local controls remain usable. No clock freshness
timeout or persistence is introduced.

At local 06:00, AUTO can start DAWN only below its target (default 60%). At
20:00 it can start DUSK only above zero. A schedule check changes MOD only.
Ramp ticks change brightness by 5%, ending at the target or zero respectively.
The current minute must match exactly: missed schedule minutes are not caught up.

Deduplication stores a date high-water mark separately for DAWN and DUSK,
including scheduled checks while AUTO is OFF. Repeated or backward-date checks
cannot replay a consumed cycle. These marks reset on reboot. AUTO ON records
its synchronized enable time and excludes the current/past scheduled minute;
it never changes brightness or starts a cycle itself. AUTO OFF immediately
sets IDLE while preserving brightness. Enabling before time sync arms future
valid checks without inventing a wall-clock timestamp.

Manual +/- remains available during ramps. It preserves MOD unless DAWN reaches
or exceeds its target, or DUSK reaches zero; these endpoints enter IDLE
immediately after a successful manual output command. DAWN never reduces an
above-target manual level. Failed ramp writes restore the previous light state
and record/log the driver error; the next periodic tick can retry.

Future Blynk work must add a dedicated virtual pin for LIGHT_A / LDR actual
light percentage. No pin is assigned here and the dashboard/API is unchanged.

Stage 5 firmware build and host FSM tests passed; new modules have no compiler
warnings. Legacy volatile-pointer/unused-OLED warnings and the 16 MB / 2 MB
Flash configuration warning remain. Stage 5 has not been tested on hardware.

### Stage 5 hardware checklist

Use UART logs; OLED actuator state integration remains deferred. There is no
production time-forcing hook, so use synchronized local schedule times and the
unchanged ten-minute ramp period. Host tests cover accelerated synthetic times.

- A: AUTO OFF at 06:00/20:00: no automatic transition or brightness change.
- B: AUTO ON before 06:00, light 20%: DAWN without an immediate step, then
  25, 30, 35, 40, 45, 50, 55, 60% on ticks and IDLE at 60%.
- C: AUTO ON before 20:00, light 50%: DUSK without an immediate step, then
  decrease by 5% on ticks to 0%, followed by IDLE.
- D: During DUSK press +: immediate +5%, still DUSK; next tick subtracts 5%.
- E: During DAWN press -: immediate -5%, still DAWN; next tick adds 5%.
- F: Turn AUTO OFF during either cycle: IDLE immediately, brightness unchanged
  through subsequent ramp ticks.
- Also check AUTO ON at 21:00 and within an already-started schedule minute:
  no catch-up. At 06:00 with brightness 80%, DAWN must not lower it to 60%.
- During DAWN raise brightness to/above target: immediate IDLE without reduction.
- Boot without SNTP access: manual pump/light/AUTO controls still work, and
  scheduled cycles remain blocked until synchronization.
- Regression: held buttons do not repeat; bounds remain 0..100%; legacy Blynk
  commands cannot override local control; sensor telemetry still operates.

## Historical Stage 4 local control

The sections below this status describe the original Stage 1 boundary.
Stage 4 now starts `pump_init()`, `light_pwm_init()`, `app_control_start()`,
and `input_task_start()` in that order, before legacy networking and sensors.
Initialization failures use ESP-IDF error checks and stop startup.

Only `control_task` commands the actuators after safe driver initialization.
Pump presses toggle ON/OFF. Light presses adjust by 5%, clamped to 0..100%.
AUTO toggles its flag without changing brightness; MOD remains IDLE.
Driver failures are logged, recorded in `last_error`, and roll back the
commanded application value to its previous successful command.

Legacy button startup, auto-light task startup, PWM initialization, and Blynk
actuator polling/writes are disabled. Their functions remain as reference.
Legacy sensors and telemetry still run. OLED sensor rows remain; actuator rows
refer to UART because display snapshot integration is deferred. No FSM,
schedule, ramp, or V0.2 network path is active.

Validation covers compilation and static ownership review, not physical hardware.
After flashing, verify OFF/0% startup, one pump toggle per press, no repeats
while held, light steps of 5% with both endpoints clamped, and AUTO toggling
without changing brightness. Test release/repress and simultaneous buttons,
and confirm Blynk commands cannot override local control. Use UART for state
and error logs. The existing Flash configuration warning remains deferred.

## Stage boundary

The default entry point still runs the proven legacy implementation in `src/main.c`.
Hardware definitions now come from `config/board_config.h`; water calibration
comes from `config/app_config.h`. Legacy timings and the legacy 0..10 light scale
are unchanged. The V0.2 application model exclusively uses 0..100 percentages.

All new C modules are compiled, but `app_control_start()` is deliberately not
called by legacy startup. This avoids introducing a second active control model
before the actuator drivers and legacy handoff have been implemented and verified.
No GPIO/PWM changes or full FSM migration are part of this stage.

## Ownership and startup contract

`app_control_start()` allocates one event queue and starts `control_task`.
Call it once, from startup, before creating producers. Startup calls are not
concurrent; a second successful start is rejected. Allocation failures are
reported, and a failed task creation releases the queue so startup can retry.

The central `app_state_t` lives inside `control_task`. No writable state pointer
or queue handle is exported. The task initializes the state (AUTO off, light and
pump off, target 60%, IDLE), then blocks on `xQueueReceive(..., portMAX_DELAY)`.
Sensor and connectivity events update this private state. Actuator and schedule
events have explicit no-op cases until their replacement subsystems exist.

Producers use `app_control_post_event()` in task context, with an explicit wait
budget. The queue copies payloads by value. Invalid types/percentages, calls
before startup, and queue timeouts are reported to the caller. Producers must
handle failures; ISR posting is not provided. Buttons will be debounced in
`input_task` before sending application events.

Display and network snapshot publication is deferred until consumers exist.
They will receive copies rather than direct access to mutable application state.
Zero sensor fields at startup are placeholders, not evidence of valid samples.

## Reserved module responsibilities

- `input_task`: button events.
- `sensor_task`: all sensor acquisition and one sensor snapshot event; no per-sensor tasks.
- `display_task`: render copied snapshots.
- `network_task`: Blynk command events and telemetry; no actuator writes.
- `time_task`: synchronized time checks every 60 seconds.
- `ramp_task`: ramp tick events every 10 minutes.
- Drivers: hardware access modules, never independent tasks.
- Light/pump FSMs: invoked only through the control path.

The placeholder modules intentionally expose no fake successful APIs and start
no tasks. Only the control task and its queue have implementations at this stage.

## Future light behavior

With AUTO enabled, a fresh 06:00 check may start DAWN and a fresh 20:00 check may
start DUSK. Repeated checks must not restart the same scheduled cycle. Each ramp
tick changes the commanded level by 5%, clamped to the DAWN target or zero for
DUSK, then returns to IDLE at the endpoint. Manual controls remain usable and
do not cancel an active ramp. AUTO OFF sets IDLE without changing brightness;
AUTO ON only arms future cycles, never a missed cycle.

LDR measurements are display/telemetry data only. They never determine PWM or
FSM transitions. Raw soil/LDR ADC readings remain available for calibration.

## Validation

Build with `platformio run -e esp32s3`. This stage checks compilation/linking;
hardware behavior and a replacement-driver handoff require later board testing.

Verified on 2026-11-03: the `esp32s3` build compiled all new modules and produced
`firmware.bin`. RAM usage was 37,808 / 327,680 bytes; the application partition
used 951,396 / 1,048,576 bytes (90.7%). Existing legacy warnings remain for
discarded `volatile` qualifiers and the unused `oled_fill` helper. No new module
warnings were reported. `git diff --check` passed. No device was flashed.
