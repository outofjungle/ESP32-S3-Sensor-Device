#include "rx_accum.h"

void rx_accum_init(rx_accum_t *a)
{
    a->len = 0;
    a->overflow = false;
}

void rx_accum_feed(rx_accum_t *a, const uint8_t *bytes, size_t n, const rx_accum_cb_t *cb, void *ctx)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t b = bytes[i];
        if (b != 0x00) {
            if (a->overflow) {
                continue; // dropped until the delimiter
            }
            if (a->len >= sizeof(a->buf)) {
                a->overflow = true;
                continue;
            }
            a->buf[a->len++] = b;
            continue;
        }
        // Delimiter: the frame is complete.
        if (a->overflow) {
            if (cb->on_oversize != NULL) {
                cb->on_oversize(ctx);
            }
        } else if (a->len >= 1) {
            if (cb->on_frame != NULL) {
                cb->on_frame(ctx, a->buf, a->len);
            }
        }
        // A stray delimiter (len == 0) is ignored.
        a->len = 0;
        a->overflow = false;
    }
}
