#!/bin/bash
# Build the Pixel 7 (panther, gs201) shim: start.S + pixel7.c + pstore.c + console.c + fdt.c +
# loader.c, wrapped into the STOCK boot.img by tools/unpack_bootv2.py --repack (our Image replaces
# the kernel, PAYLOAD replaces the ramdisk; nothing is flashed; ABL patches /chosen linux,initrd-*).
# Usage: [NDK=...] shim/build-pixel7.sh STOCK_BOOT.img [PAYLOAD]  -> shim/out/pixel7/pixel7-boot.img
set -euo pipefail
stock=$(realpath "$1")
payload=${2:+$(realpath "$2")}
cd "$(dirname "$0")"

# Cross toolchain: Android NDK (clang + lld + llvm binutils), then GNU prefixes.
NDKBIN=${NDK:-/Volumes/HARD/android-sdk/ndk/*/toolchains/llvm/prebuilt/darwin-x86_64/bin}
for d in $NDKBIN; do [ -x "$d/clang" ] && NDKBIN="$d" && break; done
if [ -x "$NDKBIN/clang" ]; then
	CC="$NDKBIN/clang -target aarch64-elf"
	OC="$NDKBIN/llvm-objcopy"; NM="$NDKBIN/llvm-nm"; RE="$NDKBIN/llvm-readelf"
else
	CC=aarch64-linux-gnu-gcc
	command -v $CC >/dev/null || CC=aarch64-elf-gcc
	OC=${CC%gcc}objcopy; NM=${CC%gcc}nm; RE=${CC%gcc}readelf
fi
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -DPIXEL7"
mkdir -p out/pixel7
for f in start.S pixel7.c pstore.c console.c fdt.c loader.c; do
	$CC $CFLAGS -c $f -o out/pixel7/${f%.*}.o
done
# No relocation pass at runtime: only start.o's image_size may be absolute.
abs=$($RE -r out/pixel7/pixel7.o out/pixel7/pstore.o out/pixel7/console.o out/pixel7/fdt.o out/pixel7/loader.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
# lld (NDK) rejects GNU ld's --no-warn-rwx-segments on older releases: retry without.
OBJS="out/pixel7/start.o out/pixel7/pixel7.o out/pixel7/pstore.o out/pixel7/console.o out/pixel7/fdt.o out/pixel7/loader.o"
RWX="-Wl,--no-warn-rwx-segments"
$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none $RWX -Wl,-N $OBJS -o out/pixel7/shim.elf || \
	$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,-N $OBJS -o out/pixel7/shim.elf
$OC -O binary out/pixel7/shim.elf out/pixel7/Image
size=$($NM out/pixel7/shim.elf | awk '$3=="_end"{print $1}')
truncate -s $((16#$size)) out/pixel7/Image
# Payload rides as the boot.img ramdisk (ABL exposes it via /chosen linux,initrd-*),
# NOT appended at _end like the Pixel 1 LK flow.
python3 ../tools/unpack_bootv2.py --repack "$stock" out/pixel7/Image out/pixel7/pixel7-boot.img ${payload:+"$payload"}
echo "shim/out/pixel7/pixel7-boot.img ready"
