# LILYGO T-Embed CC1101 Plus

**Support status:** the only runtime hardware profile supported by RFScout `main`. This page records implemented platform-specific behavior; shared resource contracts are in [Architecture](../ARCHITECTURE.md). Proposed boards and features belong only in [Roadmap](../ROADMAP.md).

## Identification and display

RFScout identifies this board through an early I²C probe of the BQ25896 charger (0x6B) or BQ27220 gauge (0x55) on SDA GPIO8 / SCL GPIO18. If no supported profile is recognized, startup leaves ordinary peripheral pins untouched for recovery flashing. The runtime profile is immutable after selection.

- MCU: ESP32-S3; display: ST7789, 320 × 170 logical pixels (170 × 320 native orientation).
- Input: rotary encoder GPIO4/GPIO5, center select GPIO0; separate user/power key GPIO6.
- Power latch: GPIO15; backlight PWM GPIO21.
- SPI bus: MOSI GPIO9, MISO GPIO10, SCLK GPIO11. Display CS GPIO41, DC GPIO16; SD CS GPIO13; CC1101 CS GPIO12; nRF24 CS GPIO44.
- CC1101 GDO2 GPIO38, GDO0 GPIO3, RF switches GPIO47/GPIO48.
- PDM microphone data GPIO42 / clock GPIO39; speaker I2S LRCLK GPIO40 / BCLK GPIO46 / data GPIO7.
- Optional MLX90640 thermal sensor at I²C 0x33. Other optional peripheral presence is validated through the respective provider's probe.

The pin values above reflect `main/services/board/board.c`. The board profile declares capabilities, not guaranteed peripheral presence.

## GPS and nRF24 UART pin sharing

The supported external GPS UART uses ESP32 GPIO43 (TX) and GPIO44 (RX), overlapping the nRF24 CE/CS control pins. GPS detection depends on valid NMEA traffic, not a position fix. The platform arbitrates access: GPS with valid NMEA has priority; an nRF24 app can use its radio when GPS is not detected. Swapping or hot-plugging these accessories while scanning is active may need manual recovery and should not be treated as lossless.

A GPS without a fix can still hold UART priority. A valid, fresh position fix is separately required before recording geographic capture metadata. The GPS driver probes supported UART baud rates and may use CASIC receiver-rate configuration when verified; see [GPS](../apps/GPS.md).

## Shared resources and operational limits

The display, SD card, CC1101 and nRF24 share SPI resources; use the board SPI arbitration rather than direct uncoordinated transfers. Wi-Fi and BLE share ESP32-S3 2.4 GHz airtime. The ESP32-S3 does not provide 5 GHz Wi-Fi reception. Storage, radio callbacks, and streaming workflows must respect the platform's session ownership and teardown rules.

The runtime supports deep sleep, screen off, and physical power-off with distinct behavior. See [Architecture](../ARCHITECTURE.md) and [Home](../apps/HOME.md). Optional hardware failure does not imply that the entire board is unsupported.
