#ifndef APP_INIT_H
#define APP_INIT_H

/**
 * @file app_init.h
 * @brief 应用初始化入口（含 FreeRTOS 队列/事件组创建与 app_tasks_init 调用）
 */

#include "config/app_types.h"

/** 创建内核对象、模组上电配置、业务任务；须在 osKernelStart() 之前调用 */
void app_init(void);

/** 对指定 SIM 槽执行切卡 + APN + PDP（可在任务上下文调用，内部会 AT） */
int app_prepare_modem(sim_slot_t sim, int force);

/** 查询指定槽 CPIN 是否 READY */
int app_modem_is_ready(sim_slot_t sim);

#endif
