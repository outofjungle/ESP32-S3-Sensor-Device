#include "device_config_logic.h"
#include "input_validate.h"
#include <string.h>

void dc_set_defaults(config_state_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = DC_CFG_VERSION;
    c->period_us = DC_PERIOD_US_DEFAULT;
    strncpy(c->mode, "PERIODIC", sizeof(c->mode) - 1);
    c->batch = 1;
    strncpy(c->led_mode, "STATUS", sizeof(c->led_mode) - 1);
    c->sub_count = 0;
    c->streaming = false;
}

bool dc_mode_valid(const char *mode)
{
    return strcmp(mode, "PERIODIC") == 0 || strcmp(mode, "ONCHANGE") == 0 || strcmp(mode, "BOTH") == 0;
}

bool dc_led_mode_valid(const char *mode)
{
    return strcmp(mode, "STATUS") == 0 || strcmp(mode, "MANUAL") == 0;
}

int dc_sanitize_loaded(config_state_t *c)
{
    int repaired = 0;
    // Never resume streaming automatically after a restart.
    c->streaming = false;
    if (!iv_u32_in_range(c->sub_count, 0, DC_MAX_SUBS)) {
        c->sub_count = DC_MAX_SUBS;
        repaired++;
    }
    iv_force_terminate(c->mode, sizeof(c->mode));
    iv_force_terminate(c->led_mode, sizeof(c->led_mode));
    for (uint32_t i = 0; i < c->sub_count; i++) {
        iv_force_terminate(c->subs[i], DC_CHANNEL_NAME_MAXLEN);
    }
    // Entries past sub_count are unused; clear them so stale bytes never show.
    for (uint32_t i = c->sub_count; i < DC_MAX_SUBS; i++) {
        c->subs[i][0] = '\0';
    }
    if (!iv_u32_in_range(c->period_us, DC_PERIOD_US_MIN, DC_PERIOD_US_MAX)) {
        c->period_us = DC_PERIOD_US_DEFAULT;
        repaired++;
    }
    if (!iv_u32_in_range(c->batch, DC_BATCH_MIN, DC_BATCH_MAX)) {
        c->batch = 1;
        repaired++;
    }
    if (!dc_mode_valid(c->mode)) {
        strncpy(c->mode, "PERIODIC", sizeof(c->mode) - 1);
        c->mode[sizeof(c->mode) - 1] = '\0';
        repaired++;
    }
    if (!dc_led_mode_valid(c->led_mode)) {
        strncpy(c->led_mode, "STATUS", sizeof(c->led_mode) - 1);
        c->led_mode[sizeof(c->led_mode) - 1] = '\0';
        repaired++;
    }
    return repaired;
}
