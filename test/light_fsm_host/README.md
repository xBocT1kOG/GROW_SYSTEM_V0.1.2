# Host Light FSM tests

These assertions compile the production `src/app/light_fsm.c` directly. The
only shim supplies the unused `esp_err_t` type required by `app_state.h`.
They do not emulate FreeRTOS, SNTP, GPIO, or PWM hardware.

From an x64 Visual Studio Developer Command Prompt at the repository root:

```bat
cl /nologo /std:c11 /W4 /WX /Itest\light_fsm_host\stubs test\light_fsm_host\test_light_fsm.c src\app\light_fsm.c /Fe.pio\light-fsm-test.exe /Fo.pio\
.pio\light-fsm-test.exe
```

The `.pio` directory must exist (a firmware build creates it).
Keep assertions enabled. No production timing constants or clock hooks change.

Coverage includes unsynchronized time, AUTO OFF, DAWN/DUSK endpoints, no
immediate step, schedule deduplication, backward clock dates, AUTO arming
without catch-up, manual changes during cycles, above-target DAWN behavior,
non-multiple endpoint clamping, and LDR independence.
