#ifndef APP_QUEUES_H
#define APP_QUEUES_H

/**
 * @file app_queues.h
 * @brief 应用层 FreeRTOS 消息队列与队列集句柄声明
 *
 * 队列角色一览：
 *   g_queue_4g_rx      : 4G/模组侧收到的完整帧 -> forward 任务
 *   g_queue_conc_rx    : RS485 集中器侧收到的帧 -> forward 任务
 *   g_queue_monitor    : 监测样本 monitor_sample_t -> decision 任务
 *   g_queue_set_forward: 上述两个接收队列的“或等待”集合，供 forward 单点阻塞
 *
 * 队列深度在 app_config.h 中配置（APP_QUEUE_LEN_*）。
 * 发送方多使用 xQueueSend(..., 0) 非阻塞；接收方 forward/decision 使用 portMAX_DELAY 阻塞。
 */

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t g_queue_4g_rx;
extern QueueHandle_t g_queue_conc_rx;
extern QueueHandle_t g_queue_monitor;
extern QueueSetHandle_t g_queue_set_forward;

#endif
