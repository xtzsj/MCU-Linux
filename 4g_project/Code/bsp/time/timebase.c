#include "timebase.h"
#include "stm32f1xx_hal.h"

uint32_t timebase_ms(void) {
    return HAL_GetTick();
}
