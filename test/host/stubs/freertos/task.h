#pragma once
#include <stdint.h>

#define pdMS_TO_TICKS(ms) ((uint32_t)(ms))
static int g_delay_ms_total = 0;
static inline void vTaskDelay(uint32_t ticks)
{
    g_delay_ms_total += (int)ticks;
}
