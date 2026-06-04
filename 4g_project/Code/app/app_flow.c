// 应用数据流程层：QGDW 帧解析、4G/RS485 透传、SIM 切换动作、监测采样。
// 这一层不创建任务，由 app_tasks 中的任务调用；涉及串口/AT 的路径须 Take g_uart_mutex。

#include "app_flow.h"

#include "config/app_config.h"
#include "drivers/at/n720v5.h"
#include "bsp/rs485/rs485.h"
#include "bsp/uart/uart_driver.h"
#include "bsp/uart/uart_rx.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32f1xx_hal.h"
#include "usart.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t address[5];
    uint8_t protocol_id;
    uint8_t control;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t checksum;
    uint16_t user_length;
} qgdw_frame_info_t;

// 以下静态缓存用于保存“主卡/备卡”各自最近一次采样值。
// 这样任务层只需要定时触发采样，本层负责记忆和组装最终样本。
static int s_last_rssi_main = 0;
static int s_last_rssi_backup = 0;
static uint8_t s_last_present_main = 0U;
static uint8_t s_last_present_backup = 0U;
static uint8_t s_last_ready_main = 0U;
static uint8_t s_last_ready_backup = 0U;
static char s_last_ccid_main[APP_AT_LINE_MAX] = {0};
static char s_last_ccid_backup[APP_AT_LINE_MAX] = {0};

// 当前 SIM 选择由任务层维护；本层只按参数处理。
static uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint8_t)(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F') {
        return (uint8_t)(10 + (c - 'A'));
    }
    return 0xFFU;
}

// 将 10 位十六进制字符串转换为 5 字节地址。
static int parse_qgdw_addr(const char *hex_text, uint8_t addr[5]) {
    size_t i;

    if (hex_text == NULL || addr == NULL || hex_text[0] == '\0') {
        return 0;
    }

    if (strlen(hex_text) != 10U) {
        return 0;
    }

    for (i = 0U; i < 5U; ++i) {
        uint8_t high = hex_nibble(hex_text[i * 2U]);
        uint8_t low = hex_nibble(hex_text[i * 2U + 1U]);
        if (high == 0xFFU || low == 0xFFU) {
            return 0;
        }
        addr[i] = (uint8_t)((high << 4U) | low);
    }

    return 1;
}

// 1 字节累加校验，和高位丢弃。
static uint8_t qgdw_checksum8(const uint8_t *data, size_t len) {
    uint32_t sum = 0U;
    size_t i;

    if (data == NULL) {
        return 0U;
    }

    for (i = 0U; i < len; ++i) {
        sum += data[i];
    }

    return (uint8_t)(sum & 0xFFU);
}

// 解析帧格式：0x68 + 长度 + 0x68 + 控制域 + 地址域(5B) + 数据域 + 校验 + 0x16。
// 这里采用“长度字段包含控制域、地址域、数据域、校验”的约定。
static int qgdw_parse_frame(const uint8_t *frame, size_t len, qgdw_frame_info_t *info) {
    uint16_t length_field;
    uint8_t received_checksum;
    uint8_t calculated_checksum;
    size_t data_offset;
    size_t data_len;
    size_t i;

    if (frame == NULL || info == NULL) {
        return 0;
    }

    if (len < 12U) {
        return 0;
    }

    if (frame[0] != 0x68U || frame[3] != 0x68U || frame[len - 1U] != 0x16U) {
        return 0;
    }

    length_field = (uint16_t)frame[1U] | ((uint16_t)frame[2U] << 8U);
    if ((length_field & 0x0003U) != 0x0002U) {
        return 0;
    }

    info->protocol_id = (uint8_t)(length_field & 0x0003U);
    info->user_length = (uint16_t)(length_field >> 2U);
    if (info->user_length < 6U) {
        return 0;
    }

    // 整帧长度 = 4 字节头 + 用户数据长度 + 1 字节尾。
    if ((size_t)(info->user_length + 5U) != len) {
        return 0;
    }

    info->control = frame[4U];
    for (i = 0U; i < 5U; ++i) {
        info->address[i] = frame[5U + i];
    }

    data_offset = 10U;
    data_len = (size_t)info->user_length - 6U;
    info->payload = &frame[data_offset];
    info->payload_len = data_len;

    received_checksum = frame[len - 2U];
    calculated_checksum = qgdw_checksum8(&frame[4U], len - 6U);
    info->checksum = received_checksum;

    return received_checksum == calculated_checksum;
}

// 从配置里读取本地集中器地址；为空则默认全匹配。
static int qgdw_local_addr_match(const uint8_t addr[5]) {
    uint8_t local_addr[5];

    if (addr == NULL) {
        return 0;
    }

    if (APP_QGDW_LOCAL_ADDR[0] == '\0') {
        return 1;
    }

    if (!parse_qgdw_addr(APP_QGDW_LOCAL_ADDR, local_addr)) {
        return 1;
    }

    return memcmp(addr, local_addr, sizeof(local_addr)) == 0;
}

// 串口调试打印：帧被丢弃的原因。
static void debug_print_drop(const char *reason) {
    char log_buf[96];
    int len;

    if (reason == NULL) {
        return;
    }

    len = snprintf(log_buf, sizeof(log_buf), "QGDW drop: %s\r\n", reason);
    if (len > 0) {
        if (len > (int)sizeof(log_buf)) {
            len = (int)sizeof(log_buf);
        }
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)log_buf, (uint16_t)len, 1000U);
    }
}

// 串口调试打印：切卡耗时。
static void debug_print_switch_time(sim_slot_t target, uint32_t start_tick, uint32_t end_tick, uint32_t switch_time) {
    char log_buf[128];
    const char *switch_name = (target == SIM_SLOT_MAIN) ? "Switch: Slave->Main" : "Switch: Main->Slave";
    int len = snprintf(log_buf, sizeof(log_buf), "%s, Start: %lu ms, End: %lu ms, Cost: %lu ms\r\n",
                       switch_name,
                       (unsigned long)start_tick,
                       (unsigned long)end_tick,
                       (unsigned long)switch_time);

    if (len > 0) {
        if (len > (int)sizeof(log_buf)) {
            len = (int)sizeof(log_buf);
        }
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)log_buf, (uint16_t)len, 1000U);
    }
}

// 与 IMSI 关联的 APN 映射。
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

// 根据主站/备站类型执行透传前的帧校验，再完成 RS485 发送。
int app_handle_forward_4g_frame(const frame_t *frame) {
    qgdw_frame_info_t qgdw_frame;

    if (frame == NULL) {
        return 0;
    }

    if (!qgdw_parse_frame(frame->data, frame->len, &qgdw_frame)) {
        debug_print_drop("CRC or frame format error");
        return 0;
    }

    if (!qgdw_local_addr_match(qgdw_frame.address)) {
        debug_print_drop("address not match local concentrator");
        return 0;
    }

    /* 互斥：与监测/切卡/反向透传共享模组口；超时 2s 避免 forward 永久阻塞 */
    if (xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return 0;
    }

    rs485_set_tx(1U);
    (void)uart_rs485_write(frame->data, frame->len);
    rs485_set_tx(0U);
    xSemaphoreGive(g_uart_mutex);
    return 1;
}

// 由集中器侧上来的数据帧，先确认 4G 网络注册状态，再透传到 4G 模块。
int app_handle_forward_concentrator_frame(const frame_t *frame) {
    int reg_stat = 0;

    if (frame == NULL) {
        return 0;
    }

    if (xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return 0;
    }

    /* 持锁期间发 AT+CREG? 与裸数据写模组，避免与其它任务的 AT 交织 */
    (void)n720v5_get_creg(&reg_stat);
    if (reg_stat == 1 || reg_stat == 5) {
        (void)uart_modem_write(frame->data, frame->len);
    }
    xSemaphoreGive(g_uart_mutex);
    return 1;
}

// 执行 SIM 切换、配置 APN、重建数据通道，并在切换结束后打印耗时。
void app_handle_sim_switch(sim_slot_t target, uint32_t start_tick) {
    char imsi[32];
    const char *apn = "CMNET";
    uint32_t end_tick;
    uint32_t switch_time;

    /* 切卡 AT 序列较长，互斥等待 5s；由 sim_switch 任务调用，优先级已较高 */
    if (xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return;
    }

    if (n720v5_set_sim(target)) {
        uart_modem_select(target);
        if (n720v5_get_cimi(imsi, sizeof(imsi))) {
            apn = apn_from_imsi(imsi);
        }
        (void)n720v5_set_cgdcont(apn);
        (void)n720v5_set_netact(APP_NET_CHANNEL, 1U);
        if (APP_SERVER_PORT > 0U) {
            (void)n720v5_netcreate(APP_NET_CHANNEL, APP_NET_MODE, APP_NET_SOCKET_ID,
                                   APP_SERVER_IP, APP_SERVER_PORT, APP_LOCAL_PORT);
        }
    }

    xSemaphoreGive(g_uart_mutex);

    end_tick = HAL_GetTick();
    if (end_tick >= start_tick) {
        switch_time = end_tick - start_tick;
    } else {
        switch_time = (0xFFFFFFFFUL - start_tick) + end_tick + 1UL;
    }
    debug_print_switch_time(target, start_tick, end_tick, switch_time);
}

// 分别经 USART1/USART3 查询主备卡状态（无需切换 $MYSIMSWITCH）。
static void monitor_probe_slot(sim_slot_t slot) {
    int rssi = 0;
    int ready = 0;
    char ccid[APP_AT_LINE_MAX] = {0};

    uart_modem_select(slot);
    uart_rx_flush_slot(slot);

    (void)n720v5_get_csq(&rssi);
    if (n720v5_get_ccid(ccid, sizeof(ccid))) {
        if (slot == SIM_SLOT_MAIN) {
            s_last_present_main = 1U;
            (void)strncpy(s_last_ccid_main, ccid, sizeof(s_last_ccid_main) - 1U);
            s_last_ccid_main[sizeof(s_last_ccid_main) - 1U] = '\0';
        } else {
            s_last_present_backup = 1U;
            (void)strncpy(s_last_ccid_backup, ccid, sizeof(s_last_ccid_backup) - 1U);
            s_last_ccid_backup[sizeof(s_last_ccid_backup) - 1U] = '\0';
        }
    } else {
        if (slot == SIM_SLOT_MAIN) {
            s_last_present_main = 0U;
            s_last_ccid_main[0] = '\0';
        } else {
            s_last_present_backup = 0U;
            s_last_ccid_backup[0] = '\0';
        }
    }

    (void)n720v5_get_cpin(&ready);
    if (slot == SIM_SLOT_MAIN) {
        s_last_rssi_main = rssi;
        s_last_ready_main = (uint8_t)ready;
    } else {
        s_last_rssi_backup = rssi;
        s_last_ready_backup = (uint8_t)ready;
    }
}

// 监测任务从这里拿到一份“已组装好的样本”，任务层只负责按周期触发调用。
int app_collect_monitor_sample(sim_slot_t current_sim, monitor_sample_t *sample) {
    int unacked = 0;
    int rest = 0;

    if (sample == NULL) {
        return 0;
    }

    /*
     * 监测持锁期间会连续访问 USART1/USART3 上主备卡 AT；
     * monitor 任务优先级最低，持锁会短暂阻塞 forward/rx 的 Take，一般可接受。
     */
    if (xSemaphoreTake(g_uart_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return 0;
    }

    monitor_probe_slot(SIM_SLOT_MAIN);
    monitor_probe_slot(SIM_SLOT_BACKUP);
    uart_modem_select(current_sim);

    if (APP_SERVER_PORT > 0U) {
        (void)n720v5_get_mynetack(APP_NET_SOCKET_ID, &unacked, &rest);
    }

    xSemaphoreGive(g_uart_mutex);

    sample->rssi_main = s_last_rssi_main;
    sample->rssi_backup = s_last_rssi_backup;
    sample->sim_present_main = s_last_present_main;
    sample->sim_present_backup = s_last_present_backup;
    sample->sim_ready_main = s_last_ready_main;
    sample->sim_ready_backup = s_last_ready_backup;
    sample->loss_rate = (rest > 0) ? (uint32_t)(unacked * 100U / (uint32_t)rest) : 0U;
    (void)strncpy(sample->ccid_main, s_last_ccid_main, sizeof(sample->ccid_main) - 1U);
    sample->ccid_main[sizeof(sample->ccid_main) - 1U] = '\0';
    (void)strncpy(sample->ccid_backup, s_last_ccid_backup, sizeof(sample->ccid_backup) - 1U);
    sample->ccid_backup[sizeof(sample->ccid_backup) - 1U] = '\0';
    sample->latency_ms = 0U;

    return 1;
}

