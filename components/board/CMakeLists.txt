idf_component_register(
    SRCS "board.c" "i2c.c" "display.c" "key.c" "vibrate.c" "led.c" "power.c" "rtc.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c driver
    PRIV_REQUIRES u8g2 esp_timer freertos esp_adc
)
