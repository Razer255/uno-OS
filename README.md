# uno-OS
An uno table top TFT display dashboard and console with desktop clock, animated icons, screensaver and a spotify controller functions

## Features

| Mode | Description |
|------|-------------|
| **Clock** | 12-hour digital clock with blinking colon, AM/PM indicator, and date |
| **Picture Frame** | Auto-scrolls all `.bmp` images from the SD card (5-second intervals) |
| **Eyes** | Cute animated eyes that look left/right and blink naturally |
| **Spotify** | Placeholder icon — coming soon! (future ESP8266 integration) |

## Hardware

- **Arduino Uno** (or Uno R3)
- **2.7" TFT LCD Touch Screen Hat** (ST7789 display + XPT2046 touch controller)
- **Micro SD card** (formatted FAT32, pre-loaded with `.bmp` images)

## Pin Assignments

| Function | Pin |
|----------|-----|
| TFT CS | 10 |
| TFT RST | 9 |
| TFT DC | 8 |
| Touch CS | A2 |
| Touch X (read) | A1 |
| Touch Y (read) | A0 |
| SD card CS | 4 |

> **Note:** Pin assignments match the most common 2.7" TFT hats. If your hat uses different pins, edit the `#define` constants at the top of `uno_tft_hat.ino`.

## Wiring

The TFT hat plugs directly onto the Arduino Uno's headers — no extra wiring needed.

1. Plug the TFT hat onto the Uno
2. Insert the micro SD card (with `.bmp` images) into the hat's SD slot
3. Connect the Uno to your computer via USB

## Installing Libraries

Open the Arduino IDE → **Sketch → Include Library → Manage Libraries...** and install:

| Library | Search in Library Manager |
|---------|--------------------------|
| Adafruit ST7789 | `Adafruit ST7789` by Adafruit |
| Adafruit GFX | `Adafruit GFX Library` by Adafruit |
| SD | `SD` by Arduino (built-in, usually pre-installed) |
| SPI | `SPI` by Arduino (built-in, usually pre-installed) |

## Preparing the SD Card

1. Format a micro SD card as **FAT32**
2. Copy `.bmp` images to the **root directory** of the SD card
3. Only **24-bit uncompressed BMP** files are supported
4. Recommended image size: 240×320 (native display resolution)

## Usage

1. Upload `uno_tft_hat.ino` to your Arduino Uno
2. Open the Serial Monitor (9600 baud) to see diagnostics
3. The dashboard appears after initialization
4. **Tap** any grid item to enter that mode
5. **Tap anywhere** on the screen to return to the dashboard

## Future Enhancements

- **Spotify integration** via ESP8266 WiFi module for music control
- Touch calibration for screens with different ADC ranges
- Support for more image formats (JPEG via TFT_eSPI)
- Additional dashboard widgets (weather, quotes, etc.)
- Adjustable picture frame timing

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Screen shows "SD card init failed" | Check SD card is inserted and formatted FAT32 |
| Touch doesn't respond | Verify hat is fully seated on Uno; check pin assignments |
| Images not displaying | Ensure images are 24-bit BMP and placed at root of SD |
| Colors look wrong | Verify hat uses ST7789 driver (not ST7735); change `Adafruit_ST7789` if needed |
