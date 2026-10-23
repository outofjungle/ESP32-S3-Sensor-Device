#pragma once

// Host-test stand-in: counts restarts instead of restarting.
static int g_restart_calls = 0;
static inline void esp_restart(void)
{
    g_restart_calls++;
}
