#pragma once
#include "network_service.h"
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include "cJSON.h"

// Strict Blynk numeric parsing; never accept partial or malformed responses.
static inline bool read_integer(const cJSON *root, const char *key, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsArray(item) && cJSON_GetArraySize(item) == 1) item = cJSON_GetArrayItem(item, 0);
    if (cJSON_IsNumber(item)) {
        double v = item->valuedouble;
        if (!isfinite(v) || v < INT_MIN || v > INT_MAX || floor(v) != v) return false;
        *value = (int)v;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring[0]) {
        char *end;
        errno = 0;
        long v = strtol(item->valuestring, &end, 10);
        if (errno || *end || v < INT_MIN || v > INT_MAX) return false;
        *value = (int)v;
        return true;
    }
    return false;
}

static inline bool network_parse_controls(const char *data, size_t length, network_controls_t *controls)
{
    cJSON *root = cJSON_ParseWithLengthOpts(data, length + 1, NULL, true);
    int pump, light, automatic;
    bool valid = cJSON_IsObject(root) && read_integer(root, "v3", &pump) &&
        read_integer(root, "v5", &light) && read_integer(root, "v7", &automatic) &&
        (pump == 0 || pump == 1) && (automatic == 0 || automatic == 1);
    cJSON_Delete(root);
    if (!valid) return false;
    *controls = (network_controls_t){pump != 0, (uint8_t)(light < 0 ? 0 : light > 100 ? 100 : light), automatic != 0};
    return true;
}
