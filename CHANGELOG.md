# Unreleased
- Releases carry `workbench-manifest.json` for web flashers, written by `tools/build_release.sh`.
- The rev3 image needs chip revision v3.1 or later (ESP-IDF 5.5's default minimum), not any v3.x.

# 1.0.0 (2026-10-17)
- First release. MPI3501 driver for ESP-IDF 5.5 on the ESP32-P4, with a 5-point touch calibrator
  that stores its result in NVS, a touch tester with an accuracy check, and 8x16 text.
- menuconfig options for pins, SPI clock and 180° rotation.
- Prebuilt images for chip revision v1.x (tested) and v3.x (untested).
