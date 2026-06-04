#include "rs485.h"
#include "stm32f1xx_hal.h"

void rs485_init(void) {
    // 默认接收状态：DE/RE 低电平
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
}

void rs485_set_tx(uint8_t enable) {
    // 高电平发送，低电平接收
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
