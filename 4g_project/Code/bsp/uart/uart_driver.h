#ifndef UART_DRIVER_H
#define UART_DRIVER_H

/**
 * @file uart_driver.h
 * @brief 串口驱动 API；含 FreeRTOS 互斥量 g_uart_mutex 声明
 */

#include <stddef.h>
#include <stdint.h>
#include "config/app_types.h"
#include "FreeRTOS.h"
#include "semphr.h"

/** 模组/RS485 共享访问互斥量，见 uart_driver.c 注释 */
extern SemaphoreHandle_t g_uart_mutex;

void uart_init(void);
void uart_sync_init(void);
void uart_modem_select(sim_slot_t sim);
sim_slot_t uart_get_modem_slot(void);
size_t uart_modem_write(const uint8_t *data, size_t len);
size_t uart_modem_read(uint8_t *data, size_t max_len, uint32_t timeout_ms);
size_t uart_modem_read_line(char *line, size_t max_len, uint32_t timeout_ms);
size_t uart_rs485_write(const uint8_t *data, size_t len);
size_t uart_rs485_read(uint8_t *data, size_t max_len, uint32_t timeout_ms);

#endif
