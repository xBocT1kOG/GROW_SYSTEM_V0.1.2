#pragma once

#include <stdbool.h>
#include <time.h>

// ---------------------------------------------------------
// SYNCHRONIZED LOCAL TIME
// ---------------------------------------------------------
typedef struct {
    bool synced;
    int year;
    int day_of_year;
    int hour;
    int minute;
    int second;
    time_t timestamp;
} time_snapshot_t;

// Register after TCP/IP initialization and before starting SNTP.
void time_service_init(void);
time_snapshot_t time_service_now(void);