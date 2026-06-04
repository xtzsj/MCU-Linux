// 供任务调用的 N720V5 AT 指令封装。
#include "n720v5.h"
#include "at_client.h"
#include "at_parser.h"
#include "bsp/uart/uart_driver.h"
#include <stdio.h>
#include <string.h>

// 从指定前缀后提取 token（如 "+CCID:"）。
static int extract_token(const char *resp, const char *prefix, char *out, size_t out_size) {
    const char *p = strstr(resp, prefix);
    size_t len = 0U;

    if (p == NULL || out == NULL || out_size == 0U) {
        return 0;
    }

    p += strlen(prefix);
    while (*p == ' ' || *p == ':') {
        ++p;
    }
    while (*p != '\r' && *p != '\n' && *p != '\0' && len + 1U < out_size) {
        out[len++] = *p++;
    }
    out[len] = '\0';
    return len > 0U;
}

// 提取首个数字串（用于 AT+CIMI 的 IMSI 解析）。
static int extract_first_digits(const char *resp, char *out, size_t out_size) {
    const char *p = resp;
    size_t len = 0U;

    if (resp == NULL || out == NULL || out_size == 0U) {
        return 0;
    }

    while (*p != '\0') {
        if (*p >= '0' && *p <= '9') {
            len = 0U;
            while (*p >= '0' && *p <= '9' && len + 1U < out_size) {
                out[len++] = *p++;
            }
            out[len] = '\0';
            return len > 0U;
        }
        ++p;
    }
    return 0;
}

// IMSI 是纯数字行，取首个数字串即可。
int n720v5_get_cimi(char *imsi, size_t imsi_size) {
    char resp[APP_AT_RESPONSE_MAX];

    if (!at_send_command("AT+CIMI", resp, sizeof(resp), 2000U)) {
        return 0;
    }
    return extract_first_digits(resp, imsi, imsi_size);
}

int n720v5_get_ccid(char *ccid, size_t ccid_size) {
    char resp[APP_AT_RESPONSE_MAX];

    if (!at_send_command("AT+CCID", resp, sizeof(resp), 2000U)) {
        return 0;
    }
    return extract_token(resp, "+CCID", ccid, ccid_size);
}

int n720v5_get_csq(int *out_rssi) {
    char resp[APP_AT_RESPONSE_MAX];
    int rssi = 0;

    if (out_rssi == NULL) {
        return 0;
    }
    if (!at_send_command("AT+CSQ", resp, sizeof(resp), 2000U)) {
        return 0;
    }
    if (!at_parse_int_after_colon(resp, 0, &rssi)) {
        return 0;
    }
    *out_rssi = rssi;
    return 1;
}

int n720v5_get_creg(int *out_stat) {
    char resp[APP_AT_RESPONSE_MAX];
    int stat = 0;

    if (out_stat == NULL) {
        return 0;
    }
    if (!at_send_command("AT+CREG?", resp, sizeof(resp), 2000U)) {
        return 0;
    }
    if (!at_parse_int_after_colon(resp, 1, &stat)) {
        return 0;
    }
    *out_stat = stat;
    return 1;
}

int n720v5_get_cpin(int *out_ready) {
    char resp[APP_AT_RESPONSE_MAX];

    if (out_ready == NULL) {
        return 0;
    }
    if (!at_send_command("AT+CPIN?", resp, sizeof(resp), 2000U)) {
        return 0;
    }
    *out_ready = (strstr(resp, "READY") != NULL) ? 1 : 0;
    return 1;
}

int n720v5_set_cgdcont(const char *apn) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];

    if (apn == NULL) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    return at_send_command(cmd, resp, sizeof(resp), 3000U);
}

int n720v5_set_sim(sim_slot_t sim) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];

    snprintf(cmd, sizeof(cmd), "AT$MYSIMSWITCH=%d", (int)sim);
    return at_send_command(cmd, resp, sizeof(resp), 5000U);
}

int n720v5_set_netact(uint8_t channel, uint8_t action) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];

    snprintf(cmd, sizeof(cmd), "AT$MYNETACT=%u,%u", channel, action);
    return at_send_command(cmd, resp, sizeof(resp), 5000U);
}

int n720v5_netcreate(uint8_t channel, uint8_t mode, uint8_t socket_id,
                     const char *ip, uint16_t port, uint16_t local_port) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];

    if (ip == NULL) {
        return 0;
    }
    snprintf(cmd, sizeof(cmd), "AT$MYNETCREATE=%u,%u,%u,\"%s\",%u,%u",
             channel, mode, socket_id, ip, (unsigned)port, (unsigned)local_port);
    return at_send_command(cmd, resp, sizeof(resp), 5000U);
}

int n720v5_get_mynetack(uint8_t socket_id, int *out_unacked, int *out_rest) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];
    int unacked = 0;
    int rest = 0;

    if (out_unacked == NULL || out_rest == NULL) {
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "AT$MYNETACK=%u", socket_id);
    if (!at_send_command(cmd, resp, sizeof(resp), 2000U)) {
        return 0;
    }
    if (!at_parse_int_after_colon(resp, 1, &unacked)) {
        return 0;
    }
    if (!at_parse_int_after_colon(resp, 2, &rest)) {
        return 0;
    }
    *out_unacked = unacked;
    *out_rest = rest;
    return 1;
}

// 当前为桩实现：仅解析长度，实际数据需要读取原始负载。
int n720v5_mynetread(uint8_t socket_id, uint8_t *data, size_t max_len, size_t *out_len) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];
    int data_len = 0;

    if (data == NULL || out_len == NULL) {
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "AT$MYNETREAD=%u,%u", socket_id, (unsigned)max_len);
    if (!at_send_command(cmd, resp, sizeof(resp), 3000U)) {
        return 0;
    }
    if (!at_parse_int_after_colon(resp, 1, &data_len)) {
        return 0;
    }
    if (data_len <= 0) {
        *out_len = 0U;
        return 1;
    }

    if ((size_t)data_len > max_len) {
        data_len = (int)max_len;
    }

    {
        size_t got = uart_modem_read(data, (size_t)data_len, 3000U);
        *out_len = got;
        return got == (size_t)data_len;
    }
}

// 发送 MYNETWRITE 后写入原始数据负载。
int n720v5_mynetwrite(uint8_t socket_id, const uint8_t *data, size_t len) {
    char cmd[APP_AT_LINE_MAX];
    char resp[APP_AT_RESPONSE_MAX];

    if (data == NULL || len == 0U) {
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "AT$MYNETWRITE=%u,%u", socket_id, (unsigned)len);
    if (!at_send_command(cmd, resp, sizeof(resp), 3000U)) {
        return 0;
    }

    uart_modem_write(data, len);
    return 1;
}
