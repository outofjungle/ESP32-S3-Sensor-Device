# The onboard RGB LED status state machine. See docs/10-led-status.md.
idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        board
        device_proto
        led_strip
        esp_timer
)
