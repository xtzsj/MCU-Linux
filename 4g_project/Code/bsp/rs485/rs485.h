#ifndef RS485_H
#define RS485_H

#include <stdint.h>

void rs485_init(void);
void rs485_set_tx(uint8_t enable);

#endif
