idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "clock_face.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        driver
)
