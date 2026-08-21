// Target definition
#define RG_TARGET_NAME              "LilyGo T-HMI"

// Storage: the T-HMI SD socket is wired for the SD_MMC 1-bit bus.
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_12
#define RG_GPIO_SDSPI_CMD           GPIO_NUM_11
#define RG_GPIO_SDSPI_D0            GPIO_NUM_13

// Audio: use the external I2S amplifier wiring from Anemoia-T-HMI.
#define RG_AUDIO_USE_INT_DAC        0
#define RG_AUDIO_USE_EXT_DAC        1
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_18
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_43
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_44

// Video: ST7789 on the 8-bit I80 bus, landscape after the display rotation.
#define RG_SCREEN_DRIVER            2
#define RG_SCREEN_BACKLIGHT         1
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATION          1
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}

// The T-HMI I80 path has enough transfer bandwidth for the native refresh rate. Disable the
// emulator cores' conservative fixed frame skip; adaptive skipping remains available if a core
// actually falls behind its timing budget.
#define RG_FORCE_FULL_FRAMERATE      1
#define RG_GPIO_LCD_CS              GPIO_NUM_6
#define RG_GPIO_LCD_DC              GPIO_NUM_7
#define RG_GPIO_LCD_WR              GPIO_NUM_8
#define RG_GPIO_LCD_D0              GPIO_NUM_48
#define RG_GPIO_LCD_D1              GPIO_NUM_47
#define RG_GPIO_LCD_D2              GPIO_NUM_39
#define RG_GPIO_LCD_D3              GPIO_NUM_40
#define RG_GPIO_LCD_D4              GPIO_NUM_41
#define RG_GPIO_LCD_D5              GPIO_NUM_42
#define RG_GPIO_LCD_D6              GPIO_NUM_45
#define RG_GPIO_LCD_D7              GPIO_NUM_46
#define RG_GPIO_LCD_BCKL            GPIO_NUM_38

// T-HMI's ST7789 initialization. The panel expects RGB ordering for Retro-Go's surfaces.
// MADCTL 0xA0 selects the landscape-flipped orientation while preserving RGB order.
#define RG_SCREEN_INIT()                                                                                         \
    ILI9341_CMD(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);                                                            \
    ILI9341_CMD(0xB7, 0x35);                                                                                     \
    ILI9341_CMD(0xBB, 0x24);                                                                                     \
    ILI9341_CMD(0xC0, 0x2C);                                                                                     \
    ILI9341_CMD(0xC2, 0x01, 0xFF);                                                                               \
    ILI9341_CMD(0xC3, 0x11);                                                                                     \
    ILI9341_CMD(0xC4, 0x20);                                                                                     \
    ILI9341_CMD(0xC6, 0x0F);                                                                                     \
    ILI9341_CMD(0xD0, 0xA4, 0xA1);                                                                               \
    ILI9341_CMD(0x36, 0xA0);                                                                                      \
    ILI9341_CMD(0xB1, 0x00, 0x10);                                                                               \
    ILI9341_CMD(0xE0, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23);    \
    ILI9341_CMD(0xE1, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23);

// Input: GPIO0 is the board button/menu key; the debug serial controller supplies the gamepad.
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_MENU, .num = GPIO_NUM_0, .pullup = 1, .level = 0},\
}
#define RG_RECOVERY_BTN              RG_KEY_MENU

// Anemoia-compatible serial packet on UART1: 0xF0, button bitfield, checksum (= bitfield).
// UART1 is exposed on GPIO43 (TX) and GPIO44 (RX) on the T-HMI.
#define RG_GAMEPAD_UART_ENABLE       1
#define RG_GAMEPAD_UART_PORT         UART_NUM_1
#define RG_GAMEPAD_UART_TX           GPIO_NUM_43
#define RG_GAMEPAD_UART_RX           GPIO_NUM_44
#define RG_GAMEPAD_UART_BAUD         115200

// Anemoia's tools/start-web-controller.ps1 uses the board USB Serial/JTAG COM port,
// which is separate from the external UART1 pins above.
#define RG_GAMEPAD_USB_SERIAL_ENABLE 1

// Battery ADC uses the T-HMI's 1:1 divider on GPIO5 (ADC1 channel 4).
#define RG_BATTERY_DRIVER             1
#define RG_BATTERY_ADC_UNIT           ADC_UNIT_1
#define RG_BATTERY_ADC_CHANNEL        ADC_CHANNEL_4
#define RG_BATTERY_CALC_PERCENT(raw)  (((raw) * 2.f - 2600.f) / (4150.f - 2600.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw)  ((raw) * 2.f * 0.001f)

// Board power rails must be enabled before display/audio/storage initialization.
#define RG_CUSTOM_PLATFORM_INIT() do {                                      \
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);                      \
    gpio_set_level(GPIO_NUM_10, 1);                                         \
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);                      \
    gpio_set_level(GPIO_NUM_14, 1);                                         \
} while (0)
