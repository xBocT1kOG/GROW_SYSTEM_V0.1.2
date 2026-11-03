#include "time_service.h"
#include <stdatomic.h>
#include "esp_sntp.h"

// ---------------------------------------------------------
// SNTP VALIDITY
// ---------------------------------------------------------
static atomic_bool synchronized;

static void on_time_sync(struct timeval *value)
{
    (void)value;
    atomic_store(&synchronized, true); // Service metadata, not application state.
}

void time_service_init(void)
{
    esp_sntp_set_time_sync_notification_cb(on_time_sync);
}

// ---------------------------------------------------------
// LOCAL TIME SNAPSHOT
// ---------------------------------------------------------
time_snapshot_t time_service_now(void)
{
    time_snapshot_t snapshot = {0};
    if (!atomic_load(&synchronized)) {
        return snapshot;
    }
    struct tm local;
    snapshot.timestamp = time(NULL);
    if (snapshot.timestamp == (time_t)-1 || localtime_r(&snapshot.timestamp, &local) == NULL ||
        local.tm_year < 120) {
        return snapshot;
    }
    snapshot.synced = true;
    snapshot.year = local.tm_year + 1900;
    snapshot.day_of_year = local.tm_yday;
    snapshot.hour = local.tm_hour;
    snapshot.minute = local.tm_min;
    snapshot.second = local.tm_sec;
    return snapshot;
}