#ifndef APP_FLOW_H
#define APP_FLOW_H

#include <stdint.h>
#include "config/app_types.h"

void app_handle_sim_switch(sim_slot_t target, uint32_t start_tick);
int app_handle_forward_4g_frame(const frame_t *frame);
int app_handle_forward_concentrator_frame(const frame_t *frame);
int app_collect_monitor_sample(sim_slot_t current_sim, monitor_sample_t *sample);

#endif

