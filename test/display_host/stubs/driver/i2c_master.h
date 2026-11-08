#pragma once
#include <stdint.h>
#include <stddef.h>
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
int i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data,
                        size_t length, int timeout);
