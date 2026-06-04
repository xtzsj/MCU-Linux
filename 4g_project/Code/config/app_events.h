#ifndef APP_EVENTS_H
#define APP_EVENTS_H

/**
 * @file app_events.h
 * @brief 应用层 FreeRTOS 事件组：用于“切卡判决”与“切卡执行”任务间异步通知
 *
 * 使用模型：
 *   task_decision  -> xEventGroupSetBits(g_app_event_group, APP_EVT_xxx)
 *   task_sim_switch -> xEventGroupWaitBits(..., 阻塞直到某位被置 1)
 *
 * 事件位可 OR 组合等待；本工程 Wait 时 xWaitForAllBits=pdFALSE，即“任一事件”触发即可。
 */

#include "FreeRTOS.h"
#include "event_groups.h"

/** bit0：请求切换到主卡（SIM_SLOT_MAIN） */
#define APP_EVT_SWITCH_TO_MAIN   (1U << 0)

/** bit1：请求切换到备卡（SIM_SLOT_BACKUP） */
#define APP_EVT_SWITCH_TO_BACKUP (1U << 1)

/** 全局事件组句柄，在 app_init.c 中 xEventGroupCreate */
extern EventGroupHandle_t g_app_event_group;

#endif
