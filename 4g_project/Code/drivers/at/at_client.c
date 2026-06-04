// 简化版 AT 客户端：发送命令、收集行并判断 OK/ERROR。
#include "at_client.h"
#include "bsp/uart/uart_driver.h"
#include "bsp/uart/uart_rx.h"
#include <string.h>

// 追加单行到响应缓存，用换行分隔。
static int append_line(char *resp, size_t resp_size, const char *line) {
    size_t cur = strlen(resp);
    size_t add = strlen(line);
    if (cur + add + 2 >= resp_size) {
        return 0;
    }
    memcpy(resp + cur, line, add);
    resp[cur + add] = '\n';
    resp[cur + add + 1] = '\0';
    return 1;
}

int at_send_command(const char *cmd, char *resp, size_t resp_size, uint32_t timeout_ms) {
    // 说明：uart_read_line 应阻塞直到收到一行或超时。
    char line[128];

    if (cmd == NULL || resp == NULL || resp_size == 0U) {
        return 0;
    }

    resp[0] = '\0';
    /* 清当前模组口环形缓冲，避免旧数据被当成 AT 响应（可在持 g_uart_mutex 后调用） */
    uart_rx_flush(UART_RX_PORT_MODEM);
    uart_modem_write((const uint8_t *)cmd, strlen(cmd));
    uart_modem_write((const uint8_t *)"\r", 1U);

    while (uart_modem_read_line(line, sizeof(line), timeout_ms) > 0U) {
        if (!append_line(resp, resp_size, line)) {
            return 0;
        }
        if (strstr(line, "OK") != NULL) {
            return 1;
        }
        if (strstr(line, "ERROR") != NULL) {
            return 0;
        }
    }

    return 0;
}

int at_wait_ok(const char *resp) {
    return (resp != NULL && strstr(resp, "OK") != NULL);
}

int at_wait_error(const char *resp) {
    return (resp != NULL && strstr(resp, "ERROR") != NULL);
}
