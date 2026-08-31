idf_component_register(
    SRCS
        "main.cpp"
        "core/riddle_decide.c"
        "core/wake_log.c"
        "core/kids.c"
        "core/weather.c"
        "core/schedule.c"
        "core/sd_json.c"
        "core/daily_layout.c"
        "core/he_text.c"
        "core/riddle_batch.c"
        "core/formdata.c"
        "core/strip.c"
        "ui/hebrew.cpp"
        "ui/page_daily.cpp"
        "ui/calibrate.cpp"
        "ui/gfx_target.cpp"
        "board/wake.cpp"
        "board/feedback.cpp"
        "board/state.cpp"
        "board/sdcard.cpp"
        "board/net.cpp"
        "board/portal.cpp"
        "board/batch.cpp"
        "board/strip_fetch.cpp"
        "board/sdconfig.cpp"
        "board/wx.cpp"
    INCLUDE_DIRS
        "."
        "core"
        "ui"
        "board"
    REQUIRES
        M5Unified
        M5PM1
        M5GFX
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp-tls
        json
        fatfs
        esp_driver_gpio
        esp_driver_usb_serial_jtag
        esp_driver_sdspi
        esp_driver_spi
        sdmmc
        vfs
        lwip
        esp_http_server
    EMBED_FILES
        "ui/font24HE.FON"
        "ui/font16HE.FON"
)

# -DPD_CALIBRATE=1 on the idf.py command line sets a CMake cache variable, not
# a compiler define, so the calibration page silently never compiled in and two
# "calibration" flashes were ordinary builds. This is what turns one into the
# other.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PD_CALIBRATE=$<IF:$<BOOL:${PD_CALIBRATE}>,1,0>)
