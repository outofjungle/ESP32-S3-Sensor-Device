#include "stream_logic.h"
#include "protocol.h"
#include "cbor.h"

uint32_t stream_seq_next(uint32_t *seq)
{
    uint32_t cur = (*seq > STREAM_SEQ_MAX) ? 0 : *seq;
    *seq = (cur >= STREAM_SEQ_MAX) ? 0 : cur + 1;
    return cur;
}

bool stream_build_sample(uint8_t *buf, size_t cap, uint32_t seq, uint64_t ts_us, const channel_entry_t *ch,
                         uint32_t flags, size_t *len)
{
    CborEncoder enc, arr;
    cbor_encoder_init(&enc, buf, cap, 0);
    cbor_encoder_create_array(&enc, &arr, 6);
    cbor_encode_uint(&arr, FRAME_KIND_DATA);
    cbor_encode_uint(&arr, seq);
    cbor_encode_uint(&arr, ts_us);
    cbor_encode_text_stringz(&arr, ch->name);
    ch->get(ch->ctx, &arr);
    cbor_encode_uint(&arr, flags);
    if (cbor_encoder_close_container(&enc, &arr) != CborNoError) {
        return false;
    }
    *len = cbor_encoder_get_buffer_size(&enc, buf);
    return true;
}
