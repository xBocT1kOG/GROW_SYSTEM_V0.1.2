# Sensor conversion and filtering tests

Run from an x64 Visual Studio Developer Command Prompt at the repository root:

```bat
cl /nologo /std:c11 /W4 /WX test\sensor_host\test_sensor_math.c /Fe.pio\sensor-test.exe /Fo.pio\
.pio\sensor-test.exe
```

The `.pio` directory must exist. Assertions must remain enabled. Tests use the
production conversion and filter headers: ADC limits/monotonicity, water
calibration, partial median history, even sample counts, ring replacement,
outlier rejection, and invalid-sample preservation. Hardware transactions,
electrical timing, task scheduling, and error injection need board testing.
