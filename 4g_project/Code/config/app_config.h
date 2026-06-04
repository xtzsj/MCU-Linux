#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

// 串口波特率（集中器要求 >=115200）。
#define APP_UART_BAUDRATE 115200U

// 周期监测间隔与切卡后锁定时间。
#define APP_MONITOR_PERIOD_MS 100U
#define APP_SWITCH_LOCK_MS 120000U

// 切卡阈值配置。
#define APP_RSSI_DIFF_THRESHOLD 3
#define APP_LATENCY_MS_THRESHOLD 500U
#define APP_LOSS_RATE_THRESHOLD 30U

#define APP_QUEUE_LEN_4G_RX 8U
#define APP_QUEUE_LEN_CONC_RX 8U
#define APP_QUEUE_LEN_MONITOR 8U

#define APP_MAX_FRAME_SIZE 512U

#define APP_AT_RESPONSE_MAX 256U
#define APP_AT_LINE_MAX 128U

// 透明通道配置（设置 IP/端口后才会启用）。
#define APP_NET_CHANNEL 1
#define APP_NET_SOCKET_ID 2
#define APP_NET_MODE 2U
#define APP_SERVER_IP "0.0.0.0"
#define APP_SERVER_PORT 0U
#define APP_LOCAL_PORT 0U

// QGDW 本地集中器地址（10 位十六进制字符串，对应 5 字节；为空则不做地址过滤）。
#define APP_QGDW_LOCAL_ADDR ""

#endif
