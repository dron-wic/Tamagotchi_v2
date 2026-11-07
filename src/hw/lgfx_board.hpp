#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board_config.h"

// ST7796 over SPI, portrait 222x480. Backlight is driven separately:
// V1.1 is a 16-step pulse on GPIO 48, not a PWM LED.
class LGFX_TDisplayS3Pro : public lgfx::LGFX_Device
{
    lgfx::Bus_SPI _bus;
    lgfx::Panel_ST7796 _panel;

public:
    LGFX_TDisplayS3Pro()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = LCD_SPI_HZ;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            // S3 GDMA raises "done" before the last bytes have left the pin.
            // A scroll rect under 1024 bytes took that path and CS rose mid-RAMWR.
            cfg.dma_channel = 0;
            cfg.pin_sclk = PIN_SPI_SCK;
            cfg.pin_mosi = PIN_SPI_MOSI;
            cfg.pin_miso = PIN_SPI_MISO;
            cfg.pin_dc = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = LCD_W;
            cfg.memory_height = LCD_H;
            cfg.panel_width = LCD_W;
            cfg.panel_height = LCD_H;
            cfg.offset_x = LCD_OFFSET_X;
            cfg.offset_y = LCD_OFFSET_Y;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};
