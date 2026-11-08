# Diagnostics host tests (Windows)

Use an x64 Visual Studio Developer Command Prompt from the repository root.
Set CJSON_DIR to the installed ESP-IDF components/json/cJSON directory.
Create .pio first if no firmware build has run. Keep assertions enabled.

```bat
cl /nologo /std:c11 /W3 /Itest\diagnostics_host\stubs /I"%CJSON_DIR%" test\diagnostics_host\test_diagnostics.c src\diagnostics\diagnostics.c src\diagnostics\diagnostics_json.c src\app\light_fsm.c "%CJSON_DIR%\cJSON.c" /Fe.pio\diagnostics-test.exe /Fo.pio\
.pio\diagnostics-test.exe
cl /nologo /std:c11 /W3 /Itest\diagnostics_host\stubs test\diagnostics_host\test_control_diagnostics.c src\diagnostics\diagnostics.c src\app\light_fsm.c /Fe.pio\control-diagnostics-test.exe /Fo.pio\
.pio\control-diagnostics-test.exe
```

The first suite compiles production counters/serialization/FSM and uses four
Windows threads to verify concurrent increments and snapshots. The second includes
the production control implementation to test real queue failure and command/ramp
instrumentation, including PWM failure rollback. Only hardware, task creation,
queue result, heap, clock, and lock primitives are stubbed. These are not board,
HTTP transport, RTOS scheduling, or endurance tests.
