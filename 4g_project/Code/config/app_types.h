#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "config/app_config.h"

typedef enum {
    SIM_SLOT_MAIN = 0,
    SIM_SLOT_BACKUP = 1
} sim_slot_t;

typedef struct {
    uint8_t data[APP_MAX_FRAME_SIZE];
    size_t len;
} frame_t;

typedef struct {
    int rssi_main;
    int rssi_backup;
    uint32_t latency_ms;
    uint32_t loss_rate;
    uint8_t sim_present_main;
    uint8_t sim_present_backup;
    uint8_t sim_ready_main;
    uint8_t sim_ready_backup;
    char ccid_main[APP_AT_LINE_MAX];
    char ccid_backup[APP_AT_LINE_MAX];
} monitor_sample_t;

#endif
