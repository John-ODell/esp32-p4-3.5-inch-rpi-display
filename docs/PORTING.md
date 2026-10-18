# Porting the 3.5" Raspberry Pi display to the ESP32-P4

How the lcdwiki **MPI3501** "3.5inch RPi Display" (ILI9486 + XPT2046), sold for the
Raspberry Pi, was made to work on the **Waveshare ESP32-P4-Module-DEV-KIT** with plain ESP-IDF.
It also explains the parts that trip people up, so you can port it to other boards.

## 1. The two boards

| | Raspberry Pi side | ESP32-P4 kit side |
|---|---|---|
| Connector | 40-pin GPIO header | 40-pin header P6, with the same 3V3/5V/GND positions as the Pi |
| Driver | Linux `fbtft` kernel driver, loaded by goodtft's `tft35a` overlay | this repo's `rpi_display` component, no OS |
| Display controller | ILI9486, 320x480, rated 32 MHz SPI | same panel |
| Touch | XPT2046 resistive, through the Linux `ads7846` driver | XPT2046 read directly over SPI |

The HAT plugs straight onto the kit, pin 1 to pin 1. Because the power pins line up with the
Pi's, no adapter or wiring is needed.

## 2. Pin translation

The panel uses eight signal pins. Each is a fixed Pi header position. On the kit, the same header
position lands on a P4 GPIO:

| Header pin | Panel signal | On a Pi (BCM) | On the P4 kit |
|---|---|---|---|
| 11 | TP_IRQ (touch pen-down) | GPIO17 | GPIO21 |
| 18 | LCD_RS (data/command) | GPIO24 | GPIO4 |
| 19 | MOSI, LCD and touch | GPIO10 (SPI0 MOSI) | GPIO3 |
| 21 | MISO, touch only | GPIO9 (SPI0 MISO) | GPIO2 |
| 22 | LCD reset | GPIO25 | GPIO1 |
| 23 | SCK | GPIO11 (SPI0 SCLK) | GPIO0 |
| 24 | LCD_CS | GPIO8 (SPI0 CE0) | GPIO36 |
| 26 | TP_CS | GPIO7 (SPI0 CE1) | GPIO32 |
| 1, 17 | 3V3 | | 3V3 |
| 2, 4 | 5V, which powers the backlight | | 5V |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND | | GND |

The P4 GPIO numbers come from the kit's schematic and silkscreen. They are confirmed on hardware:
the panel, reset, D/C and touch all work. The P4 can route SPI to any GPIO, so these pins need no
special function. They are just what the header happens to carry.

Pi header pins 8 and 10, the UART, are not connected on the panel, so they stay free.

## 3. The catch: it is not a plain SPI ILI9486

A bare ILI9486 takes 8-bit SPI commands. This board does not. The Pi overlay gives it away:

```
compatible = "ilitek,ili9486"   buswidth = 8   regwidth = 16   txbuflen = 32768   fps = 30
dc-gpios = <GPIO24>   reset-gpios = <GPIO25>
```

`regwidth = 16` means every register write is a 16-bit word. TFT_eSPI's documentation describes
this board family as the "Waveshare design": a 16-bit serial interface built from 74HC04,
74HC4040 and two 74HC4094 shift registers in front of the ILI9486's parallel bus. There is no
public schematic for the MPI3501, so the exact chips are inferred, not confirmed. The behaviour is
confirmed on hardware.

What that means in code:

| Write | Bytes on the wire | D/C pin |
|---|---|---|
| command `cc` | `0x00 cc` | low |
| parameter `pp` | `0x00 pp` | high |
| pixel | RGB565, big-endian, 2 bytes | high |

- Send a command as a single byte and the panel stays white. This is the most common reason
  "ILI9486 driver X doesn't work with this screen".
- There is **no read-back**. MISO is driven only by the touch controller, so the ID can't be read
  and the driver can't probe the panel.
- The D/C pin is set before each SPI transaction, in a pre-transfer callback.

## 4. Init sequence

The register values were decoded from the binary `tft35a-overlay.dtb` in goodtft's LCD-show
repository. Its `init` property uses fbtft's encoding: `0x1000000 | cmd` is a command, a plain
value is a parameter, and `0x2000000 | ms` is a delay. The values were rewritten as a C table in
`rpi_lcd.c`:

| Command | Parameters | Purpose |
|---|---|---|
| `F1` `F2` `F8` `F9` | vendor values | undocumented ILI9486 tuning, copied as-is |
| `36` | `08` | memory access control, BGR order |
| `B4` | `00` | display inversion control |
| `C1` | `41` | power control 2 |
| `C5` | `00 91 80 00` | VCOM control |
| `E0` / `E1` | 15 bytes each | positive / negative gamma |
| `3A` | `55` | 16-bit RGB565 pixels |
| `11` | | sleep out |
| `36` | `28` | MV=1 + BGR: 480x320 landscape (`E8` = landscape rotated 180°) |
| 255 ms delay | | |
| `29` | | display on |

Drawing uses the standard window commands: `2A` (columns), `2B` (rows), then `2C` and the pixel data.

## 5. Speed

A full frame is 480 x 320 x 16 bits, about 2.46 Mbit. SPI bandwidth is the only limit:

| SPI clock | Full frame | fps | Notes |
|---|---|---|---|
| 16 MHz | ~154 ms | 6.5 | measured during bring-up |
| 26.67 MHz | 92.8 ms | 10.8 | measured; this is what "32 MHz" gives on the P4 |

The P4's SPI clock divides 80 MHz, so a 32 MHz request becomes 80/3 = 26.67 MHz. The next step
up, 40 MHz, is above the panel's 32 MHz rating. Pixels go out in DMA strips of 16 rows
(15,360 bytes in internal RAM) using polling transmits. Hitting the ceiling is easy. To get more
fps, redraw only what changed.

## 6. Touch: XPT2046

- It shares the SPI bus with the LCD and has its own chip select (header pin 26). It runs at 2 MHz,
  because the XPT2046 is slow.
- Readings use 12-bit differential mode: `D0` = X, `90` = Y, `B0`/`C0` = Z1/Z2 for pressure.
  Each axis is the median of 5 samples.
- The pen-down IRQ (header pin 11) reads low while touched, so the driver doesn't poll the
  controller when nobody is touching.

### Why calibration is affine

In landscape, the touch panel's axes are swapped and mirrored relative to the pixels. Screen X
follows raw Y, and screen Y follows raw X. Units also differ from panel to panel. A plain
scale-and-offset per axis gets this wrong, so the calibrator fits a full affine map:

```
px = a*rx + b*ry + c
py = d*rx + e*ry + f
```

It uses a least-squares fit over 5 taps: the four corners and the centre. A typical result from
the test kit is `px = -0.0032*rx - 0.1338*ry + 534.6`. The large `b` term is the axis swap. If the
worst point is more than 12 px off, the calibrator assumes a bad tap and asks again. The result
goes to NVS (namespace `touch`, key `cal`, tagged with the magic number `TCA1`), so it survives
reboots and app reflashes. Because the map is affine, it also absorbs rotation. Recalibrate after
changing it.

## 7. Board-specific gotchas

- **GPIO36 is a strapping pin** on the P4, and LCD_CS lands on it. Normal boot only depends on
  GPIO35, so running is fine. Download mode needs GPIO36 high. If flashing ever fails with the
  panel fitted, unplug it to flash, or use the kit's USB-Serial-JTAG port. It has not happened on
  the test kit.
- **Leave GPIO37/38 alone.** They are UART0 to the kit's USB-to-UART chip, the console and flashing port.
- **Chip revision.** Early kits carry ESP32-P4 revision v1.x. ESP-IDF 5.5 builds for v3.x by
  default, and that bootloader refuses older chips. Use `sdkconfig.rev1` or the `-rev1` image.
- **Backlight** is always on from 5V. The panel has no backlight control pin.

## 8. Porting to another ESP32 board

1. Wire the eight signals to any free GPIOs, and power the panel with 3V3, 5V and GND.
2. Set the pins in `idf.py menuconfig`, under **MPI3501 3.5in RPi display**.
3. Pick the SPI clock. Most ESP32 SPI peripherals divide 80 MHz, so expect 26.67 MHz for a
   32 MHz request. Use 16 MHz on long jumper wires.
4. Keep internal DMA-capable RAM free for the 15 KB pixel strip.
5. Calibrate. The affine fit handles whatever orientation your wiring ends up in.

Only the ESP32-P4 kit has been tested.

## Sources

- lcdwiki, "3.5inch RPi Display" (MPI3501): pinout and SPI rating.
- goodtft/LCD-show, `usr/tft35a-overlay.dtb`: fbtft parameters and init sequence.
- TFT_eSPI documentation: the 16-bit "Waveshare design" interface for RPi-format ILI9486 boards.
- Waveshare ESP32-P4-Module-DEV-KIT schematic: header P6 pin to GPIO mapping.
