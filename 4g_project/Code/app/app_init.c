/**
 * @file    app_init.c
 * @brief   应用初始化：在调度器启动前创建 FreeRTOS 内核对象（队列、事件组、队列集）
 *
 * 调用时机（main.c）：
 *   HAL/串口初始化 -> app_init() -> osKernelInitialize() -> MX_FREERTOS_Init() -> osKernelStart()
 *
 * 注意：
 *   - xQueueCreate / xEventGroupCreate 可在 vTaskStartScheduler 之前调用（堆来自 configTOTAL_HEAP_SIZE）
 *   - app_tasks_init() 也在本函数末尾调用，同样早于 osKernelStart
 *   - module_init() 内 AT 指令在单线程上下文执行，尚未与其他任务争抢 g_uart_mutex
 */

#include "app_init.h"
#include "app_tasks.h"
#include "qgdw_framer.h"
#include "config/app_config.h"
#include "config/app_events.h"
#include "config/app_queues.h"
#include "config/app_types.h"
#include "drivers/at/n720v5.h"
#include "bsp/uart/uart_driver.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "event_groups.h"
#include <string.h>

/* ---------- 全局 RTOS 句柄：其它 .c 通过 app_queues.h / app_events.h 声明引用 ---------- */

/** 切卡事件组：decision 任务 SetBits，sim_switch 任务 WaitBits */
EventGroupHandle_t g_app_event_group = NULL;

/** 4G 侧下行帧队列：task_rx 写入，task_forward 经队列集读出 */
QueueHandle_t g_queue_4g_rx = NULL;

/** 集中器 RS485 上行帧队列：task_rx 写入，task_forward 经队列集读出 */
QueueHandle_t g_queue_conc_rx = NULL;

/** 监测样本队列：task_monitor 写入，task_decision 阻塞读取 */
QueueHandle_t g_queue_monitor = NULL;

/**
 * 转发队列集：将 g_queue_4g_rx 与 g_queue_conc_rx 合并为一个“多路复用等待点”
 * 长度 = 两队列深度之和，是 xQueueCreateSet 的要求（可容纳成员队列上的最大消息总数）
 */
QueueSetHandle_t g_queue_set_forward = NULL;

static const char *apn_from_imsi(const char *imsi) {
    if (imsi == NULL || imsi[0] == '\0') {
        return "CMNET";
    }
    if (strncmp(imsi, "46000", 5) == 0 || strncmp(imsi, "46002", 5) == 0 ||
        strncmp(imsi, "46007", 5) == 0) {
        return "CMNET";
    }
    if (strncmp(imsi, "46001", 5) == 0) {
        return "UNINET";
    }
    if (strncmp(imsi, "46003", 5) == 0) {
        return "CTNET";
    }
    return "CMNET";
}

static int modem_setup_on_slot(sim_slot_t sim) {
    char imsi[32];
    const char *apn = "CMNET";

    uart_modem_select(sim);
    if (n720v5_get_cimi(imsi, sizeof(imsi))) {
        apn = apn_from_imsi(imsi);
    }

    if (!n720v5_set_cgdcont(apn)) {
        return 0;
    }
    if (!n720v5_set_netact(APP_NET_CHANNEL, 1U)) {
        return 0;
    }
    if (APP_SERVER_PORT > 0U) {
        if (!n720v5_netcreate(APP_NET_CHANNEL, APP_NET_MODE, APP_NET_SOCKET_ID,
                              APP_SERVER_IP, APP_SERVER_PORT, APP_LOCAL_PORT)) {
            return 0;
        }
    }
    return 1;
}

/** 上电默认主卡：设置 $MYSIMSWITCH 并建立 PDP/可选 TCP 通道（调度器启动前执行） */
static void module_init(void) {
    (void)n720v5_set_sim(SIM_SLOT_MAIN);
    (void)modem_setup_on_slot(SIM_SLOT_MAIN);
}

int app_modem_is_ready(sim_slot_t sim) {
    int ready = 0;

    uart_modem_select(sim);
    if (!n720v5_get_cpin(&ready)) {
        return 0;
    }
    return ready;
}

int app_prepare_modem(sim_slot_t sim, int force) {
    (void)force;

    if (!n720v5_set_sim(sim)) {
        return 0;
    }
    return modem_setup_on_slot(sim);
}

void app_init(void) {
    /* 互斥量：多任务访问模组 AT/透传串口前必须 Take（见 uart_driver.c） */
    uart_sync_init();
    qgdw_framer_init();

    /*
     * xQueueCreate(uxQueueLength, uxItemSize)
     * - 每项为 sizeof(frame_t) 或 sizeof(monitor_sample_t)，队列中存的是结构体副本
     * - 深度见 app_config.h 中 APP_QUEUE_LEN_*
     */
    g_app_event_group = xEventGroupCreate();
    g_queue_4g_rx = xQueueCreate(APP_QUEUE_LEN_4G_RX, sizeof(frame_t));
    g_queue_conc_rx = xQueueCreate(APP_QUEUE_LEN_CONC_RX, sizeof(frame_t));
    g_queue_monitor = xQueueCreate(APP_QUEUE_LEN_MONITOR, sizeof(monitor_sample_t));

    g_queue_set_forward = xQueueCreateSet(APP_QUEUE_LEN_4G_RX + APP_QUEUE_LEN_CONC_RX);
    /* 成员队列必须逐个加入集合，之后才能被 xQueueSelectFromSet 监听到 */
    xQueueAddToSet(g_queue_4g_rx, g_queue_set_forward);
    xQueueAddToSet(g_queue_conc_rx, g_queue_set_forward);

    module_init();
    /* 创建 task_rx / forward / decision / monitor / sim_switch 及软件定时器 */
    app_tasks_init();
}
