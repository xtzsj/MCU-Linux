/**
 * @file uart_driver.c
 * @brief 串口发送与模组口选择；g_uart_mutex 供多任务互斥访问 AT/透传
 */
#include "uart_driver.h"
#include "uart_rx.h"
#include "stm32f1xx_hal.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

/**
 * 全局互斥量：同一时刻只允许一个任务持有并访问“当前选中的模组 UART + AT 序列”
 * 使用者：app_flow（透传/切卡/监测）、task_rx（TCP 读）、at_client（发 AT 前 flush）
 * 规则：Take 与 Give 必须成对；阻塞超时返回 pdFALSE 时不得 Give
 */
SemaphoreHandle_t g_uart_mutex = NULL;

static UART_HandleTypeDef *s_modem_uart = &huart1;
static sim_slot_t s_modem_slot = SIM_SLOT_MAIN;

/**
 * @brief 创建 UART 互斥量（在 app_init 最先调用，早于任务创建）
 * @note  xSemaphoreCreateMutex 从 FreeRTOS 堆分配；堆大小见 FreeRTOSConfig.h
 */
void uart_sync_init(void) {
    if (g_uart_mutex == NULL) {
        g_uart_mutex = xSemaphoreCreateMutex();
    }
}

void uart_init(void) {
    uart_rx_init();
}

void uart_modem_select(sim_slot_t sim) {
    s_modem_slot = sim;
    s_modem_uart = (sim == SIM_SLOT_MAIN) ? &huart1 : &huart3;
    uart_rx_set_active_modem(sim);
    uart_rx_on_modem_uart_changed(s_modem_uart);
}

sim_slot_t uart_get_modem_slot(void) {
    return s_modem_slot;
}

static size_t uart_write(UART_HandleTypeDef *huart, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (huart == NULL || data == NULL || len == 0U) {
        return 0U;
    }
    if (HAL_UART_Transmit(huart, (uint8_t *)data, (uint16_t)len, timeout_ms) != HAL_OK) {
        return 0U;
    }
    return len;
}

size_t uart_modem_write(const uint8_t *data, size_t len) {
    return uart_write(s_modem_uart, data, len, 2000U);
}

size_t uart_modem_read(uint8_t *data, size_t max_len, uint32_t timeout_ms) {
    return uart_rx_read_raw_modem(data, max_len, timeout_ms);
}

size_t uart_modem_read_line(char *line, size_t max_len, uint32_t timeout_ms) {
    return uart_rx_read_line_modem(line, max_len, timeout_ms);
}

size_t uart_rs485_write(const uint8_t *data, size_t len) {
    return uart_write(&huart2, data, len, 2000U);
}

size_t uart_rs485_read(uint8_t *data, size_t max_len, uint32_t timeout_ms) {
    return uart_rx_pop(UART_RX_PORT_RS485, data, max_len, timeout_ms);
}
