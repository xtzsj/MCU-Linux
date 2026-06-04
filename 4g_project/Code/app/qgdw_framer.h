#ifndef QGDW_FRAMER_H
#define QGDW_FRAMER_H

#include "config/app_types.h"
#include "bsp/uart/uart_rx.h"

void qgdw_framer_init(void);
int qgdw_framer_poll(uart_rx_port_t port, frame_t *out);

#endif
