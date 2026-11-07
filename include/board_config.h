#pragma once

// LilyGO T-Display-S3-Pro, portrait 222x480.
// Pin map matches the official utilities.h from Xinyuan-LilyGO/T-Display-S3-Pro.

#define LCD_W 222
#define LCD_H 480

#define PIN_I2C_SDA 5
#define PIN_I2C_SCL 6

#define PIN_SPI_MISO 8
#define PIN_SPI_MOSI 17
#define PIN_SPI_SCK 18
#define PIN_TFT_CS 39
#define PIN_TFT_DC 9
#define PIN_TFT_RST 47
#define PIN_TFT_BL 48

#define PIN_SD_CS 14
#define PIN_TOUCH_RST 13
#define PIN_TOUCH_IRQ 21

#define PIN_BTN_BACK 0
#define PIN_BTN_OK 12
#define PIN_BTN_NEXT 16

#define PIN_LED 38

// Camera shield (OV2640 / OV5640 DVP). Pins match LilyGO utilities.h.
#define PIN_CAM_PWDN 46
#define PIN_CAM_RESET -1
#define PIN_CAM_XCLK 11
#define PIN_CAM_SIOD PIN_I2C_SDA
#define PIN_CAM_SIOC PIN_I2C_SCL
#define PIN_CAM_Y9 4
#define PIN_CAM_Y8 10
#define PIN_CAM_Y7 3
#define PIN_CAM_Y6 1
#define PIN_CAM_Y5 42
#define PIN_CAM_Y4 40
#define PIN_CAM_Y3 41
#define PIN_CAM_Y2 45
#define PIN_CAM_VSYNC 7
#define PIN_CAM_HREF 15
#define PIN_CAM_PCLK 2
#define CAM_XCLK_HZ 20000000

// The buzzer on a free GPIO. -1 keeps the LED as the only feedback.
// GPIO 10 is Y8 on the camera shield.
#define PIN_BUZZER -1

// ST7796 column offset used by this panel. If the picture is shifted
// sideways, try 0 or 35.
#define LCD_OFFSET_X 49
#define LCD_OFFSET_Y 0

// 40 MHz is the safe default. 60 MHz is fine on many boards and raises
// full-frame transitions closer to 30 FPS. Drop it if the image tears.
#define LCD_SPI_HZ 40000000

#define TOUCH_ADDR 0x5A

// 1 shows a small FPS counter. Five taps on it cycle the touch map.
#define SHOW_FPS 1

// Demo pace (30 s). The live period is the settings stepper
// "Темп нужд": 30 s / 5 min / 30 min, stored in the save.
#define NEED_PERIOD_SEC 30

// Screen sleeps by turning the backlight off.
// On battery the CPU drops to 80 MHz and only a GPIO key wakes it.
// On external power the CPU stays at 240 MHz and a touch wakes it too.
#define IDLE_DIM_MS 45000
#define IDLE_OFF_MS 120000
#define BACKLIGHT_ACTIVE 14
#define BACKLIGHT_DIM 4

// 80 MHz only while the screen is off and the board is on battery.
// Dropping the clock with the charger connected leaves the touch bus dead.
#define POWER_SCALE_CPU 1
