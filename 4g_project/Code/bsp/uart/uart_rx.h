#ifndef UART_RX_H
#define UART_RX_H

#include <stddef.h>
#include <stdint.h>
#include "config/app_types.h"

typedef enum {
    UART_RX_PORT_MODEM = 0,
    UART_RX_PORT_RS485 = 1
} uart_rx_port_t;

struct __UART_HandleTypeDef;
void uart_rx_on_modem_uart_changed(struct __UART_HandleTypeDef *huart);
void uart_rx_set_active_modem(sim_slot_t sim);
void uart_rx_init(void);
size_t uart_rx_read_line_slot(sim_slot_t sim, char *line, size_t max_len, uint32_t timeout_ms);
size_t uart_rx_pop_modem_combined(uint8_t *out, size_t max_len, uint32_t timeout_ms);
void uart_rx_flush(uart_rx_port_t port);
void uart_rx_flush_slot(sim_slot_t sim);
size_t uart_rx_pop(uart_rx_port_t port, uint8_t *out, size_t max_len, uint32_t timeout_ms);
size_t uart_rx_read_line_modem(char *line, size_t max_len, uint32_t timeout_ms);
size_t uart_rx_read_raw_modem(uint8_t *data, size_t len, uint32_t timeout_ms);

#endif
