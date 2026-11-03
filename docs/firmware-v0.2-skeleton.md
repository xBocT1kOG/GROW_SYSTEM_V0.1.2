# Firmware V0.2 skeleton

## Current status: Stage 4 local control

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
