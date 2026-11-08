#pragma once
#include <stdbool.h>

// Last observed cloud values, including our own successful uploads.
typedef struct {
    int values[3];
    bool valid[3];
} network_sync_t;

static inline bool network_sync_changed(const network_sync_t *sync, unsigned index, int value)
{
    return !sync->valid[index] || sync->values[index] != value;
}

static inline void network_sync_observe(network_sync_t *sync, unsigned index, int value)
{
    sync->values[index] = value;
    sync->valid[index] = true;
}
