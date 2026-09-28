#!/bin/sh
# fetch-limine.sh - holt die festgelegte Limine-Version und prüft jede Datei,
# die ins Boot-Image geht, gegen boot/limine.sha256.
# Aufruf: tools/fetch-limine.sh <zielverzeichnis>
set -eu
dir=$1
sums=$(pwd)/boot/limine.sha256
tag=$(sed -n 's/^# tag: *//p' "$sums")
if [ ! -d "$dir/.git" ]; then
    rm -rf "$dir"
    git clone --quiet --depth=1 --branch="$tag" \
        https://github.com/limine-bootloader/limine.git "$dir"
fi
(cd "$dir" && grep -v '^#' "$sums" | sha256sum --check --quiet)
echo "limine $tag: checksums ok"
