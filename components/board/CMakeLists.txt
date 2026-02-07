idf_component_register(
    SRCS "board.c" "i2c.c" "display.c" "key.c" "vibrate.c" "led.c" "power.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver u8g2 esp_timer freertos esp_adc
)
