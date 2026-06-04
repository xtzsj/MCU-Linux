#ifndef APP_TASKS_H
#define APP_TASKS_H

/**
 * @file app_tasks.h
 * @brief 应用 FreeRTOS 任务入口声明
 *
 * app_tasks_init() 创建全部业务任务，须在 osKernelStart() 之前由 app_init() 调用。
 */

#include "config/app_types.h"

void app_tasks_init(void);

/** 供 app_flow / 外部查询当前选用的 SIM 槽位（与 task_sim_switch 维护的变量一致） */
sim_slot_t app_get_current_sim(void);

#endif
