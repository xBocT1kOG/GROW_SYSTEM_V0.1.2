#pragma once
#include <windows.h>
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
typedef SRWLOCK portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED SRWLOCK_INIT
#define portENTER_CRITICAL(lock) AcquireSRWLockExclusive(lock)
#define portEXIT_CRITICAL(lock) ReleaseSRWLockExclusive(lock)
