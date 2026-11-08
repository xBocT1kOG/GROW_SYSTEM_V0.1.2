# OLED host checks

From an x64 Visual Studio Developer Command Prompt at the repository root:

```bat
cl /nologo /std:c11 /W3 /Itest\display_host\stubs /Itest\light_fsm_host\stubs test\display_host\test_display.c /Fe.pio\display-test.exe /Fo.pio\
.pio\display-test.exe
```

Requires `.pio` to exist; keep assertions enabled. Tests compile the production
formatter and custom OLED library with a recording I2C transport. They check
eight state-derived fields, invalid sensors, endpoint values, mode labels,
padding after shrinking values, supported font characters, 60-pixel cell
widths, original page/column commands, init bytes, and full-screen clearing.
They do not verify electrical operation, the task scheduler, or real bus faults.
