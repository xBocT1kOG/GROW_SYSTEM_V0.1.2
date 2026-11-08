# Host network protocol checks

In an x64 Visual Studio Developer Command Prompt, set `CJSON_DIR` to the
installed ESP-IDF `components/json/cJSON` directory, then run from the repo root:

```bat
cl /nologo /std:c11 /W3 /Itest\light_fsm_host\stubs /I"%CJSON_DIR%" test\network_host\test_network.c "%CJSON_DIR%\cJSON.c" /Fe.pio\network-test.exe /Fo.pio\
.pio\network-test.exe
```

Uses the production parser and baseline tracking with synthetic values, never
credentials. Checks numeric/string/array responses, malformed data, fractional
commands, boolean validation, clamping, ignored MOD, unchanged output on errors,
and cloud echo suppression. Real Wi-Fi, HTTP/TLS failures, task timing and
physical actuator behavior require hardware tests. Keep assertions enabled.
