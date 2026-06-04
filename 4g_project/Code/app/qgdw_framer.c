// QGDW 帧组帧：0x68 + 长度 + 0x68 + 用户数据 + 校验 + 0x16
#include "qgdw_framer.h"
#include "config/app_config.h"
#include <string.h>

typedef struct {
    uint8_t buf[APP_MAX_FRAME_SIZE];
    size_t len;
    uint16_t expect_len;
    uint8_t started;
} qgdw_asm_t;

static qgdw_asm_t s_asm_modem;
static qgdw_asm_t s_asm_rs485;

static void asm_reset(qgdw_asm_t *asm_state) {
    if (asm_state == NULL) {
        return;
    }
    asm_state->len = 0U;
    asm_state->expect_len = 0U;
    asm_state->started = 0U;
}

static uint16_t qgdw_frame_total_len(const uint8_t *buf, size_t len) {
    uint16_t user_len;

    if (len < 4U) {
        return 0U;
    }
    user_len = (uint16_t)buf[1U] | ((uint16_t)buf[2U] << 8U);
    user_len = (uint16_t)(user_len >> 2U);
    if (user_len < 6U) {
        return 0U;
    }
    return (uint16_t)(user_len + 5U);
}

static int asm_feed_byte(qgdw_asm_t *asm_state, uint8_t byte) {
    if (asm_state == NULL) {
        return 0;
    }

    if (!asm_state->started) {
        if (byte != 0x68U) {
            return 0;
        }
        asm_state->buf[0] = byte;
        asm_state->len = 1U;
        asm_state->started = 1U;
        return 0;
    }

    if (asm_state->len >= APP_MAX_FRAME_SIZE) {
        asm_reset(asm_state);
        return 0;
    }

    asm_state->buf[asm_state->len++] = byte;

    if (asm_state->len == 4U) {
        if (asm_state->buf[3] != 0x68U) {
            asm_reset(asm_state);
            return 0;
        }
        asm_state->expect_len = qgdw_frame_total_len(asm_state->buf, asm_state->len);
        if (asm_state->expect_len < 12U || asm_state->expect_len > APP_MAX_FRAME_SIZE) {
            asm_reset(asm_state);
            return 0;
        }
    }

    if (asm_state->expect_len > 0U && asm_state->len >= asm_state->expect_len) {
        if (asm_state->buf[asm_state->len - 1U] == 0x16U) {
            return 1;
        }
        asm_reset(asm_state);
    }

    return 0;
}

void qgdw_framer_init(void) {
    asm_reset(&s_asm_modem);
    asm_reset(&s_asm_rs485);
}

int qgdw_framer_poll(uart_rx_port_t port, frame_t *out) {
    qgdw_asm_t *asm_state;
    uint8_t chunk[32];
    size_t n;
    size_t i;

    if (out == NULL) {
        return 0;
    }

    asm_state = (port == UART_RX_PORT_RS485) ? &s_asm_rs485 : &s_asm_modem;
    if (port == UART_RX_PORT_MODEM) {
        n = uart_rx_pop_modem_combined(chunk, sizeof(chunk), 0U);
    } else {
        n = uart_rx_pop(port, chunk, sizeof(chunk), 0U);
    }
    for (i = 0U; i < n; ++i) {
        if (asm_feed_byte(asm_state, chunk[i])) {
            (void)memcpy(out->data, asm_state->buf, asm_state->len);
            out->len = asm_state->len;
            asm_reset(asm_state);
            return 1;
        }
    }

    return 0;
}
