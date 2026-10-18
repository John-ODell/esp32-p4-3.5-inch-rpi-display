# Third-party material

- `components/rpi_display/font8x16.h`: an 8x16 raster of DejaVu Sans Mono, taken from
  ValentinStars/esp32p4-linux (MIT). DejaVu fonts are under the Bitstream Vera / DejaVu license,
  which is permissive.
- The ILI9486 register values in `rpi_lcd.c` follow the `tft35a` device-tree overlay from goodtft's
  LCD-show, written as a fresh C table.
- ESP-IDF (Apache-2.0) is not included. It is needed to build, and the prebuilt images link it.
