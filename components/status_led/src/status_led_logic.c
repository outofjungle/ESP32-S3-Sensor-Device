#include "status_led_logic.h"
#include <string.h>

void led_state_color(status_led_state_t state, bool phase_on, uint8_t out[3])
{
    switch (state) {
    case LED_STATE_INIT:
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        break;
    case LED_STATE_READY:
        // Green, 10% brightness.
        out[0] = 0;
        out[1] = 26;
        out[2] = 0;
        break;
    case LED_STATE_STREAM_IDLE:
        out[0] = 0;
        out[1] = 0;
        out[2] = phase_on ? 255 : 0;
        break;
    case LED_STATE_STREAM_PRESENT:
        out[0] = phase_on ? 255 : 0;
        out[1] = phase_on ? 140 : 0;
        out[2] = 0;
        break;
    case LED_STATE_FAULT:
    default:
        out[0] = 255;
        out[1] = 0;
        out[2] = 0;
        break;
    }
}

void led_pick_rgb(bool flashing, bool manual, const uint8_t manual_rgb[3], status_led_state_t state, bool phase_on,
                  uint8_t out[3])
{
    if (flashing) {
        out[0] = out[1] = out[2] = 255;
    } else if (manual) {
        memcpy(out, manual_rgb, 3);
    } else {
        led_state_color(state, phase_on, out);
    }
}

void led_tick_init(led_tick_t *t)
{
    t->state = LED_STATE_INIT;
    t->blink_counter = 0;
    t->blink_phase_on = true;
    t->flash_ticks_remaining = 0;
}

void led_tick_set_state(led_tick_t *t, status_led_state_t state)
{
    t->state = state;
    t->blink_counter = 0;
    t->blink_phase_on = true;
}

void led_tick_start_flash(led_tick_t *t)
{
    t->flash_ticks_remaining = LED_FLASH_TICKS + 1;
}

bool led_tick_flashing(const led_tick_t *t)
{
    return t->flash_ticks_remaining > 0;
}

bool led_tick_step(led_tick_t *t)
{
    bool need_render = false;
    if (t->flash_ticks_remaining > 0) {
        t->flash_ticks_remaining--;
        need_render = true;
    }
    t->blink_counter++;
    if (t->blink_counter >= LED_BLINK_TOGGLE_TICKS) {
        t->blink_counter = 0;
        t->blink_phase_on = !t->blink_phase_on;
        if (t->state == LED_STATE_STREAM_IDLE || t->state == LED_STATE_STREAM_PRESENT) {
            need_render = true;
        }
    }
    return need_render;
}
