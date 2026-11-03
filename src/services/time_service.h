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

// Register before legacy SNTP starts; timezone must be configured first.
void time_service_init(void);
time_snapshot_t time_service_now(void);