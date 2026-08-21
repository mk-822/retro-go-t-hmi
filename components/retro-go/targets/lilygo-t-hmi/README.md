# LilyGo T-HMI target

This target ports the retro-go ODROID-GO application stack to the LilyGo T-HMI
ESP32-S3 board, using the same hardware assignments as Anemoia-T-HMI:

- ST7789 LCD: 8-bit I80, CS 6, DC 7, WR 8, data 48/47/39/40/41/42/45/46, backlight 38
- SD card: SD_MMC 1-bit, CLK 12, CMD 11, D0 13
- I2S amplifier: BCLK 18, WS 43, DATA 44
- Board power: enable 10, hold 14
- Battery ADC: GPIO5 / ADC1 channel 4 with a 1:1 divider
- Menu/recovery button: GPIO0

## Build

From the repository root, inside an ESP-IDF environment:

```text
python rg_tool.py build --target lilygo-t-hmi launcher retro-core
python rg_tool.py build-img --target lilygo-t-hmi launcher retro-core
```

## Debug serial controller

The Anemoia Web Serial controller (`Anemoia-T-HMI/tools/start-web-controller.ps1`)
uses the board USB Serial/JTAG COM port. The target also accepts the same packets
on UART1 GPIO43 (TX) and GPIO44 (RX) at 115200 baud.

Packet format:

```text
0xF0, buttons, buttons
```

The button byte uses bit 0=A, 1=B, 2=Select, 3=Start, 4=Up, 5=Down,
6=Left, and 7=Right. The last valid state is held for ten input polls, then
all buttons are released, matching Anemoia's `UartControllerRead` behavior.
