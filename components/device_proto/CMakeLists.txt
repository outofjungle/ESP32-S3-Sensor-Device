# The transport and wire protocol: COBS + CRC-16/CCITT-FALSE + CBOR
# framing, the dynamic channel registry, device-level stream configuration
# (CFG), and the extensible command dispatcher. Knows nothing about
# sensors -- see docs/01-architecture.md.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES
        tinycbor
    PRIV_REQUIRES
        esp_tinyusb
        nvs_flash
        esp_timer
        app_update
        esp_hw_support
        esp_ringbuf
        freertos
)
