// USART 中断收字节入环形缓冲，供组帧与 AT 行解析使用。
#include "uart_rx.h"
#include "config/app_types.h"
#include "stm32f1xx_hal.h"
#include <string.h>

#define UART_RX_RING_SIZE 512U

typedef struct {
    uint8_t buf[UART_RX_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t it_byte;
} uart_ring_t;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

static uart_ring_t s_ring_modem_main;
static uart_ring_t s_ring_modem_backup;
static uart_ring_t s_ring_rs485;
static volatile sim_slot_t s_active_modem = SIM_SLOT_MAIN;

static uart_ring_t *modem_ring_active(void) {
    return (s_active_modem == SIM_SLOT_MAIN) ? &s_ring_modem_main : &s_ring_modem_backup;
}

static uart_ring_t *ring_for_port(uart_rx_port_t port) {
    if (port == UART_RX_PORT_RS485) {
        return &s_ring_rs485;
    }
    return modem_ring_active();
}

static void ring_push_isr(uart_ring_t *ring, uint8_t byte) {
    uint16_t next = (uint16_t)((ring->head + 1U) % UART_RX_RING_SIZE);
    if (next != ring->tail) {
        ring->buf[ring->head] = byte;
        ring->head = next;
    }
}

static int ring_pop(uart_ring_t *ring, uint8_t *byte) {
    if (ring->head == ring->tail) {
        return 0;
    }
    *byte = ring->buf[ring->tail];
    ring->tail = (uint16_t)((ring->tail + 1U) % UART_RX_RING_SIZE);
    return 1;
}

static void ring_flush(uart_ring_t *ring) {
    ring->head = 0U;
    ring->tail = 0U;
}

static void start_rx_it(UART_HandleTypeDef *huart, uart_ring_t *ring) {
    if (huart == NULL || ring == NULL) {
        return;
    }
    (void)HAL_UART_Receive_IT(huart, &ring->it_byte, 1U);
}

static void enable_usart_irq(IRQn_Type irq) {
    HAL_NVIC_SetPriority(irq, 6U, 0U);
    HAL_NVIC_EnableIRQ(irq);
}

void uart_rx_on_modem_uart_changed(UART_HandleTypeDef *huart) {
    (void)huart;
}

void uart_rx_set_active_modem(sim_slot_t sim) {
    s_active_modem = sim;
}

void uart_rx_init(void) {
    ring_flush(&s_ring_modem_main);
    ring_flush(&s_ring_modem_backup);
    ring_flush(&s_ring_rs485);
    s_active_modem = SIM_SLOT_MAIN;

    start_rx_it(&huart1, &s_ring_modem_main);
    start_rx_it(&huart2, &s_ring_rs485);
    start_rx_it(&huart3, &s_ring_modem_backup);

    enable_usart_irq(USART1_IRQn);
    enable_usart_irq(USART2_IRQn);
    enable_usart_irq(USART3_IRQn);
}

void uart_rx_flush_slot(sim_slot_t sim) {
    if (sim == SIM_SLOT_MAIN) {
        ring_flush(&s_ring_modem_main);
    } else {
        ring_flush(&s_ring_modem_backup);
    }
}

void uart_rx_flush(uart_rx_port_t port) {
    if (port == UART_RX_PORT_RS485) {
        ring_flush(&s_ring_rs485);
        return;
    }
    ring_flush(modem_ring_active());
}

size_t uart_rx_pop(uart_rx_port_t port, uint8_t *out, size_t max_len, uint32_t timeout_ms) {
    uart_ring_t *ring = ring_for_port(port);
    size_t got = 0U;
    uint32_t start = HAL_GetTick();

    if (out == NULL || max_len == 0U) {
        return 0U;
    }

    while (got < max_len) {
        uint8_t ch = 0U;
        if (ring_pop(ring, &ch)) {
            out[got++] = ch;
            continue;
        }
        if ((HAL_GetTick() - start) >= timeout_ms) {
            break;
        }
    }

    return got;
}

// 从指定卡槽环形缓冲取一行（监测双卡时使用）。
size_t uart_rx_read_line_slot(sim_slot_t sim, char *line, size_t max_len, uint32_t timeout_ms) {
    uart_ring_t *ring = (sim == SIM_SLOT_MAIN) ? &s_ring_modem_main : &s_ring_modem_backup;
    size_t idx = 0U;
    uint32_t start = HAL_GetTick();

    if (line == NULL || max_len < 2U) {
        return 0U;
    }

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t ch = 0U;
        if (!ring_pop(ring, &ch)) {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            continue;
        }
        if (idx + 1U < max_len) {
            line[idx++] = (char)ch;
        }
    }

    line[idx] = '\0';
    return idx;
}

size_t uart_rx_read_line_modem(char *line, size_t max_len, uint32_t timeout_ms) {
    return uart_rx_read_line_slot(s_active_modem, line, max_len, timeout_ms);
}

size_t uart_rx_read_raw_modem(uint8_t *data, size_t len, uint32_t timeout_ms) {
    return uart_rx_pop(UART_RX_PORT_MODEM, data, len, timeout_ms);
}

// 组帧时合并读取主/备模组缓冲中的字节。
size_t uart_rx_pop_modem_combined(uint8_t *out, size_t max_len, uint32_t timeout_ms) {
    size_t got = 0U;
    uint32_t start = HAL_GetTick();
    uint8_t ch = 0U;

    if (out == NULL || max_len == 0U) {
        return 0U;
    }

    while (got < max_len) {
        if (ring_pop(&s_ring_modem_main, &ch) || ring_pop(&s_ring_modem_backup, &ch)) {
            out[got++] = ch;
            continue;
        }
        if ((HAL_GetTick() - start) >= timeout_ms) {
            break;
        }
    }
    return got;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart1) {
        ring_push_isr(&s_ring_modem_main, s_ring_modem_main.it_byte);
        start_rx_it(huart, &s_ring_modem_main);
        return;
    }
    if (huart == &huart3) {
        ring_push_isr(&s_ring_modem_backup, s_ring_modem_backup.it_byte);
        start_rx_it(huart, &s_ring_modem_backup);
        return;
    }
    if (huart == &huart2) {
        ring_push_isr(&s_ring_rs485, s_ring_rs485.it_byte);
        start_rx_it(huart, &s_ring_rs485);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart1) {
        start_rx_it(huart, &s_ring_modem_main);
        return;
    }
    if (huart == &huart3) {
        start_rx_it(huart, &s_ring_modem_backup);
        return;
    }
    if (huart == &huart2) {
        start_rx_it(huart, &s_ring_rs485);
    }
}
