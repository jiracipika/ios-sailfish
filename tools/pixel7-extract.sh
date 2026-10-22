#!/bin/bash
# Pixel 7 (panther/gs201) extraction helper — see docs/PIXEL7-PORT.md §9.
# Pulls the device data and firmware shopping list the port needs. Everything
# lands in backup/ and research/ (both .gitignored — never commit vendor
# binaries or per-device dumps). Fails soft: each section explains itself.
#
#   tools/pixel7-extract.sh [IPSW_URL]
#
# IPSW_URL is optional; with it, only DeviceTree.d22ap.im4p is downloaded
# (needs python3 + network). Device sections need: adb/fastboot, and either a
# rooted stock build or a custom recovery booted (NOT flashed).
set -u
cd "$(dirname "$0")/.."
mkdir -p backup/research research/ipsw-x research/firmware

say() { printf '\n== %s\n' "$*"; }

have() { command -v "$1" >/dev/null 2>&1; }

adbd() { adb devices 2>/dev/null | awk 'NR==2 && $2!=""{print "yes"}'; }

say "1. Toolchain check"
for t in adb fastboot python3; do
    if have "$t"; then echo "   ok: $t"; else echo "   MISSING: $t"; fi
done
DEV=$(adbd)
if [ "${DEV:-}" = yes ]; then echo "   device connected over adb"; else
    echo "   no adb device — boot the phone to stock (with root) or a recovery"
    echo "   (e.g. 'fastboot boot <recovery>.img') and re-run for sections 2-3."
fi

say "2. Boot images (boot, dtbo, vbmeta, vendor_boot) -> backup/"
if [ "${DEV:-}" = yes ]; then
    for part in boot dtbo vbmeta vendor_boot; do
        if adb shell su -c "dd if=/dev/block/by-name/$part of=/sdcard/$part.img" 2>/dev/null \
           || adb shell "dd if=/dev/block/by-name/$part of=/sdcard/$part.img" 2>/dev/null; then
            adb pull "/sdcard/$part.img" "backup/$part.img" >/dev/null && echo "   pulled: $part.img"
        else
            echo "   FAILED: $part (need root or recovery shell) — pull it manually"
        fi
    done
    echo "   next: python3 tools/unpack_bootv2.py backup/boot.img dtb/boot_unpacked"
    echo "   (boot v4 note: Pixel 7 images may need a v4-aware unpacker — see docs §1)"
else
    echo "   skipped (no device)"
fi

say "3. Vendor firmware shopping list -> research/firmware/"
cat <<'LIST'
   Wi-Fi/BT (BCM4389, from /vendor/dlkm or /vendor/firmware):
     brcm/brcmfmac4389c1-pcie*.bin or bcmdhd fw, *.clm_blob, nvram txt
   GPU (Mali-G710 CSF): mali_csffw.bin
   (deferred) modem: firmware/* for the Exynos 5300 — only needed for M8
LIST
if [ "${DEV:-}" = yes ]; then
    mkdir -p research/firmware
    adb shell su -c "ls /vendor/dlkm/lib/modules /vendor/firmware/brcm /vendor/firmware/mali* 2>/dev/null" 2>/dev/null \
      | tee research/firmware/vendor-listing.txt \
      && echo "   listing saved: research/firmware/vendor-listing.txt (pull files of interest individually)"
fi

say "4. Apple device tree (iPhone10,3 / D22AP identity)"
if [ $# -ge 1 ]; then
    URL="$1"
    python3 tools/remotezip.py "$URL" DeviceTree.d22ap.im4p research/ipsw-x/dt \
        && ipsw img4 im4p extract research/ipsw-x/dt/Firmware/all_flash/DeviceTree.d22ap.im4p \
             -o research/ipsw-x/d22ap.adt \
        && echo "   done: research/ipsw-x/d22ap.adt" \
        || echo "   FAILED — need the 'ipsw' tool (blacktop/ipsw); same recipe as the root README"
else
    echo "   pass the iPhone_4.7... no — iPhone X (iPhone10,3) 16.7.16 (20H392) IPSW URL:"
    echo "   tools/pixel7-extract.sh 'https://.../iPhone10,3,2_16.7.16_20H392_Restore.ipsw'"
fi

say "5. References to keep open (public, no RE needed)"
cat <<'LIST'
   android.googlesource.com/kernel/gs        (pantah android13-5.10 branch)
   android.googlesource.com/kernel/gs        (+ modules: acpm, ufs-exynos, decon,
                                              dsim, dwc3-exynos, exynos-pcie, s2mpu)
   android.googlesource.com/device/google/pantah   (DTS: memory, GIC, UFS reg,
                                              CPU MPIDRs, panel s6e3hc3, touch)
LIST
echo
echo "Done. Nothing in backup/ or research/ ever goes to git."
