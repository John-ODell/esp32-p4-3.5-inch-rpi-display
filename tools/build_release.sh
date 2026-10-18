#!/bin/sh
# Build both chip-revision variants and single-file flash images into dist/.
# Needs an exported ESP-IDF 5.5 environment (". $IDF_PATH/export.sh").
set -e
cd "$(dirname "$0")/.."
VER=$(sed -n 's/^version: "\(.*\)"/\1/p' components/rpi_display/idf_component.yml)
mkdir -p dist
for REV in rev1 rev3; do
    B=build-$REV
    if [ $REV = rev1 ]; then DEFS="sdkconfig.defaults;sdkconfig.rev1"; else DEFS="sdkconfig.defaults"; fi
    idf.py -B $B -D SDKCONFIG=$B/sdkconfig -D SDKCONFIG_DEFAULTS="$DEFS" build >/dev/null
    OUT=dist/p4-rpi-display-$VER-$REV.bin
    (cd $B && python -m esptool --chip esp32p4 merge_bin -o ../$OUT @flash_args >/dev/null)
    echo "$OUT  $(wc -c < $OUT | tr -d ' ') bytes"
done
(cd dist && shasum -a 256 p4-rpi-display-$VER-*.bin > SHA256SUMS)
