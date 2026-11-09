#include "device.h"
#include "driver/i2c_master.h"
#include <string.h>
static i2c_master_dev_handle_t screen;
static uint8_t last_brightness = 255;
esp_err_t oled_init(void) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t pins = {.i2c_port = I2C_NUM_0,
                                    .sda_io_num = GPIO_NUM_21,
                                    .scl_io_num = GPIO_NUM_22,
                                    .clk_source = I2C_CLK_SRC_DEFAULT,
                                    .glitch_ignore_cnt = 7,
                                    .flags.enable_internal_pullup = true};
    esp_err_t err = i2c_new_master_bus(&pins, &bus);
    if (err != ESP_OK)
        return err;
    uint8_t addr = 0;
    for (uint8_t a = 0x3c; a <= 0x3d; a++)
        if (i2c_master_probe(bus, a, 100) == ESP_OK) {
            addr = a;
            break;
        }
    if (!addr)
        return ESP_ERR_NOT_FOUND;
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 100000};
    err = i2c_master_bus_add_device(bus, &config, &screen);
    if (err != ESP_OK)
        return err;
    // Set segment and COM direction for the installed panel orientation.
    const uint8_t init[] = {0x00, 0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0xad, 0x8b, 0xa1,
                            0xc8, 0xda, 0x12, 0x81, 0x60, 0xd9, 0x22, 0xdb, 0x35, 0xa4, 0xa6, 0xaf};
    return i2c_master_transmit(screen, init, sizeof(init), 100);
}
esp_err_t oled_present(const glance_canvas_t *canvas, uint8_t brightness) {
    if (!screen)
        return ESP_ERR_INVALID_STATE;
    if (brightness != last_brightness) {
        const uint8_t cmd[] = {0, 0x81, brightness};
        esp_err_t e = i2c_master_transmit(screen, cmd, sizeof(cmd), 100);
        if (e != ESP_OK)
            return e;
        last_brightness = brightness;
    }
    for (uint8_t page = 0; page < 8; page++) {
        const uint8_t pos[] = {0, 0xb0 | page, 0x02, 0x10}; // Proven SH1106 two-column offset.
        esp_err_t e = i2c_master_transmit(screen, pos, sizeof(pos), 100);
        if (e != ESP_OK)
            return e;
        uint8_t pixels[129] = {0x40};
        memcpy(pixels + 1, canvas->pixels + page * 128, 128);
        e = i2c_master_transmit(screen, pixels, sizeof(pixels), 100);
        if (e != ESP_OK)
            return e;
    }
    return ESP_OK;
}
