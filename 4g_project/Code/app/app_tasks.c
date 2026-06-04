/**
 * @file    app_tasks.c
 * @brief   应用层 FreeRTOS 任务、软件定时器与同步对象
 *
 * =============================================================================
 * 一、整体架构（事件驱动 + 队列解耦）
 * =============================================================================
 *
 *   [USART 中断收字节] --> 环形缓冲 --> task_rx 组帧 --> 消息队列
 *                                                      |
 *                                                      v
 *                                            task_forward 透传
 *                                                      ^
 *   task_monitor --(队列)--> task_decision --(事件组)--> task_sim_switch
 *        ^  周期定时器 + 二值信号量唤醒
 *
 * 二、任务优先级（数值越大越高，本项目 1~4）
 *   sim_switch(4) > rx(3) = forward(3) > decision(2) > monitor(1)
 *   切卡涉及 AT 与网络重建，需尽快执行；监测可让路。
 *
 * 三、同步手段
 *   - g_uart_mutex（互斥量，uart_driver.c）：串口/AT 与透传互斥
 *   - g_app_event_group（事件组）：decision -> sim_switch 切卡命令
 *   - g_queue_*（队列）：rx -> forward、monitor -> decision
 *   - g_queue_set_forward（队列集）：forward 阻塞等待任一路接收队列
 *   - g_monitor_wakeup（二值信号量）：定时器唤醒 monitor，避免空转
 *   - g_switch_locked + 单次定时器：切卡后冷却 APP_SWITCH_LOCK_MS（默认 2 分钟）
 *
 * 四、与文档《电网主要内容》对应关系（节选）
 *   - 2.1 非切换不占用模组：sim_switch 在 xEventGroupWaitBits 上阻塞
 *   - 3.2 切卡条件：decision 中 RSSI 差/延迟/丢包率阈值
 *   - 5   切换后禁止重复切换：set_switch_lock + switch_locked 检查
 */

#include "app_tasks.h"
#include "app_flow.h"
#include "qgdw_framer.h"
#include "config/app_config.h"
#include "config/app_events.h"
#include "config/app_queues.h"
#include "config/app_types.h"
#include "drivers/at/n720v5.h"
#include "bsp/uart/uart_driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "timers.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/* 本文件私有的 RTOS 对象                                                     */
/* -------------------------------------------------------------------------- */

/** 二值信号量：monitor 阻塞于此；monitor_timer 周期 Give 唤醒 */
static SemaphoreHandle_t g_monitor_wakeup = NULL;

/** 自动重载软件定时器，周期 APP_MONITOR_PERIOD_MS（默认 100ms） */
static TimerHandle_t g_monitor_timer = NULL;

/** 单次软件定时器：切卡后启动，APP_SWITCH_LOCK_MS 到期清除 g_switch_locked */
static TimerHandle_t g_switch_lock_timer = NULL;

/** 切卡冷却标志（定时器回调与 decision 任务间可见，故 volatile） */
static volatile uint8_t g_switch_locked = 0U;

/** 当前业务使用的 SIM，与模组 $MYSIMSWITCH 状态应对齐 */
static sim_slot_t g_current_sim = SIM_SLOT_MAIN;

static void task_rx(void *param);
static void task_sim_switch(void *param);
static void task_forward(void *param);
static void task_decision(void *param);
static void task_monitor(void *param);

sim_slot_t app_get_current_sim(void) {
    return g_current_sim;
}

/** 是否处于切卡冷却；为真时 decision 不再 xEventGroupSetBits */
static int switch_locked(void) {
    return g_switch_locked != 0U;
}

/**
 * @brief 切卡锁定定时器回调（Timer Service 任务上下文，非 ISR）
 */
static void switch_lock_timer_cb(TimerHandle_t timer) {
    (void)timer;
    g_switch_locked = 0U;
}

/**
 * @brief 监测周期到：Give 信号量，唤醒 task_monitor
 */
static void monitor_timer_cb(TimerHandle_t timer) {
    (void)timer;
    if (g_monitor_wakeup != NULL) {
        (void)xSemaphoreGive(g_monitor_wakeup);
    }
}

/**
 * @brief 进入切卡冷却（文档：切换后 2 分钟禁止重复切换）
 * @note  APP_SWITCH_LOCK_MS = 120000；单次定时器到期后 switch_lock_timer_cb 清零
 */
static void set_switch_lock(void) {
    g_switch_locked = 1U;
    if (g_switch_lock_timer != NULL) {
        (void)xTimerStop(g_switch_lock_timer, 0U);
        (void)xTimerChangePeriod(g_switch_lock_timer, pdMS_TO_TICKS(APP_SWITCH_LOCK_MS), 0U);
        (void)xTimerStart(g_switch_lock_timer, 0U);
    }
}

/**
 * @brief TCP 模式读 socket，送入 g_queue_4g_rx
 * @note  Take 互斥量超时 10ms，避免与切卡/监测长时间 AT 死等
 */
static void rx_poll_tcp_frames(void) {
    uint8_t buf[128];
    size_t got = 0U;
    frame_t frame;

    if (APP_SERVER_PORT == 0U) {
        return;
    }

    if (xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    while (n720v5_mynetread(APP_NET_SOCKET_ID, buf, sizeof(buf), &got) && got > 0U) {
        if (got > APP_MAX_FRAME_SIZE) {
            got = APP_MAX_FRAME_SIZE;
        }
        frame.len = got;
        (void)memcpy(frame.data, buf, got);
        (void)xQueueSend(g_queue_4g_rx, &frame, 0);
    }

    xSemaphoreGive(g_uart_mutex);
}

/**
 * @task task_rx（优先级 3）
 * @brief 从环形缓冲组 QGDW 帧并入队；vTaskDelay(5) 避免忙等占满 CPU
 */
static void task_rx(void *param) {
    frame_t frame;

    (void)param;
    for (;;) {
        if (qgdw_framer_poll(UART_RX_PORT_RS485, &frame)) {
            (void)xQueueSend(g_queue_conc_rx, &frame, 0);
        }

        if (qgdw_framer_poll(UART_RX_PORT_MODEM, &frame)) {
            (void)xQueueSend(g_queue_4g_rx, &frame, 0);
        }

        rx_poll_tcp_frames();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/**
 * @task task_sim_switch（优先级 4，业务任务最高）
 * @brief SIM 切换任务：无切卡事件时阻塞，不占用 CPU、不跑模组 AT
 *
 * xEventGroupWaitBits 参数（对应文档 2.1）：
 *   - APP_EVT_SWITCH_TO_MAIN  = BIT0（主卡）
 *   - APP_EVT_SWITCH_TO_BACKUP = BIT1（副卡）
 *   - xClearOnExit = pdTRUE    ：唤醒后自动清除事件位，避免重复切卡
 *   - xWaitForAllBits = pdFALSE：BIT0 或 BIT1 任一即可唤醒
 *   - portMAX_DELAY            ：无事件永久阻塞
 *
 * 唤醒后流程：
 *   app_handle_sim_switch() -> 互斥量 + AT 五步（见 app_flow.c / n720v5.c）
 *   set_switch_lock()       -> 2 分钟冷却
 *   循环回到 WaitBits      -> 再次阻塞等待
 */
static void task_sim_switch(void *param) {
    uint32_t start_tick;

    (void)param;
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(
            g_app_event_group,
            APP_EVT_SWITCH_TO_MAIN | APP_EVT_SWITCH_TO_BACKUP,
            pdTRUE,         /* 等到后清除标志位 */
            pdFALSE,        /* 主卡 OR 副卡事件 */
            portMAX_DELAY); /* 无事件休眠 */

        start_tick = HAL_GetTick();
        if (bits & APP_EVT_SWITCH_TO_MAIN) {
            app_handle_sim_switch(SIM_SLOT_MAIN, start_tick);
            g_current_sim = SIM_SLOT_MAIN;
            uart_modem_select(SIM_SLOT_MAIN);
            set_switch_lock();
        }
        if (bits & APP_EVT_SWITCH_TO_BACKUP) {
            app_handle_sim_switch(SIM_SLOT_BACKUP, start_tick);
            g_current_sim = SIM_SLOT_BACKUP;
            uart_modem_select(SIM_SLOT_BACKUP);
            set_switch_lock();
        }
    }
}

/**
 * @task task_forward（优先级 3）
 * @brief 在队列集上阻塞；4G 或集中器队列有帧则调用 app_flow 透传
 *
 * xQueueSelectFromSet：任一路 g_queue_4g_rx / g_queue_conc_rx 有数据即返回
 * xQueueReceive(..., 0)：已由 Select 通知有数据，此处非阻塞取走 frame_t
 */
static void task_forward(void *param) {
    (void)param;

    for (;;) {
        QueueSetMemberHandle_t member = xQueueSelectFromSet(g_queue_set_forward, portMAX_DELAY);

        if (member == g_queue_4g_rx) {
            frame_t frame;
            if (xQueueReceive(g_queue_4g_rx, &frame, 0) == pdTRUE) {
                (void)app_handle_forward_4g_frame(&frame);
            }
        } else if (member == g_queue_conc_rx) {
            frame_t frame;
            if (xQueueReceive(g_queue_conc_rx, &frame, 0) == pdTRUE) {
                (void)app_handle_forward_concentrator_frame(&frame);
            }
        }
    }
}

/**
 * @task task_decision（优先级 2）
 * @brief 仅当监测样本满足切卡条件时 xEventGroupSetBits 唤醒 sim_switch
 *
 * 条件（app_config.h，对应文档 3.2）：
 *   当前主卡：rssi_main - rssi_backup >= 3，或 latency > 500ms，或丢包率 > 阈值
 *   当前副卡：rssi_backup - rssi_main >= 3 则切回主卡
 *
 * switch_locked() 为真时本周期不 SetBits（2 分钟冷却内）
 */
static void task_decision(void *param) {
    (void)param;

    for (;;) {
        monitor_sample_t sample;
        if (xQueueReceive(g_queue_monitor, &sample, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (switch_locked()) {
            continue;
        }

        if (g_current_sim == SIM_SLOT_MAIN) {
            if ((sample.rssi_main - sample.rssi_backup) >= APP_RSSI_DIFF_THRESHOLD ||
                sample.latency_ms > APP_LATENCY_MS_THRESHOLD ||
                sample.loss_rate > APP_LOSS_RATE_THRESHOLD) {
                xEventGroupSetBits(g_app_event_group, APP_EVT_SWITCH_TO_BACKUP);
            }
        } else {
            if ((sample.rssi_backup - sample.rssi_main) >= APP_RSSI_DIFF_THRESHOLD) {
                xEventGroupSetBits(g_app_event_group, APP_EVT_SWITCH_TO_MAIN);
            }
        }
    }
}

/**
 * @task task_monitor（优先级 1，最低）
 * @brief 等 g_monitor_wakeup 信号量 -> 采样 -> xQueueSend 给 decision
 */
static void task_monitor(void *param) {
    (void)param;

    for (;;) {
        monitor_sample_t sample = {0};

        if (g_monitor_wakeup != NULL) {
            (void)xSemaphoreTake(g_monitor_wakeup, portMAX_DELAY);
        }

        if (app_collect_monitor_sample(g_current_sim, &sample)) {
            (void)xQueueSend(g_queue_monitor, &sample, 0);
        }
    }
}

/**
 * @brief 创建任务、定时器、信号量；须在 osKernelStart() 之前由 app_init() 调用
 *
 * xTaskCreate 第三参数为栈深度（字），如 512 表示 512*4 字节栈
 * sim_switch 栈 512、优先级 4：切卡 AT 序列较深
 */
void app_tasks_init(void) {
    g_monitor_wakeup = xSemaphoreCreateBinary();
    if (g_monitor_wakeup != NULL) {
        (void)xSemaphoreTake(g_monitor_wakeup, 0U); /* 保证首次由定时器触发 */
    }

    g_monitor_timer = xTimerCreate(
        "monitor_tick",
        pdMS_TO_TICKS(APP_MONITOR_PERIOD_MS),
        pdTRUE,  /* 周期定时器 */
        NULL,
        monitor_timer_cb);
    g_switch_lock_timer = xTimerCreate(
        "switch_lock",
        pdMS_TO_TICKS(APP_SWITCH_LOCK_MS),
        pdFALSE, /* 单次：冷却结束回调一次 */
        NULL,
        switch_lock_timer_cb);

    xTaskCreate(task_rx, "rx", 384U, NULL, 3U, NULL);
    xTaskCreate(task_sim_switch, "sim_switch", 512U, NULL, 4U, NULL);
    xTaskCreate(task_forward, "forward", 384U, NULL, 3U, NULL);
    xTaskCreate(task_decision, "decision", 256U, NULL, 2U, NULL);
    xTaskCreate(task_monitor, "monitor", 384U, NULL, 1U, NULL);

    if (g_monitor_timer != NULL) {
        (void)xTimerStart(g_monitor_timer, 0U);
    }
}
