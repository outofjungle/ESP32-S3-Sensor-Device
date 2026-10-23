#pragma once
#include <stdint.h>

// Host-test stand-in: a clock the test controls.
static int64_t g_fake_time_us = 123456;
static inline int64_t esp_timer_get_time(void)
{
    return g_fake_time_us;
}
