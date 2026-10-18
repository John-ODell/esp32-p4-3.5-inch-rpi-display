# Unreleased
- Releases carry `workbench-manifest.json` for web flashers, written by `tools/build_release.sh`.
- Docs: the rev1 image boots v1.x chips (not v0.x) and the rev3 image needs v3.1 or later, as the builds set.
- Fix: the 8x16 font's `0` had a filled middle and read as an `8`; it is now a slashed zero. The v1.0.0 release images still carry the old glyph.

# 1.0.0 (2026-10-17)
- First release. MPI3501 driver for ESP-IDF 5.5 on the ESP32-P4, with a 5-point touch calibrator
  that stores its result in NVS, a touch tester with an accuracy check, and 8x16 text.
- menuconfig options for pins, SPI clock and 180° rotation.
- Prebuilt images for chip revision v1.x (tested) and v3.x (untested).
