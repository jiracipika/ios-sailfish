# Pixel 7 first boot — runbook

Everything upstream of the phone is done and testable without a device (shim builds, image
packaging, FDT walker tests). This is what to do the day the bootloader is unlocked, and what
each outcome means. Prereqs: M0 kernel build green (Xcode 15.4 + KDK staged — docs/LOCAL-BUILD.md)
and a payload from `tools/mkpayload.py` (needs M1 values; until then use `tools/mkstub-payload.py`
+ `shim/out/pixel7/stub.bin`-style stub to prove the boot path).

## 1. Package

```sh
# stock boot.img from the phone (M1 pack; tools/pixel7-extract.sh pulls it)
tools/unpack_bootv2.py backup/boot.img dtb/boot_unpacked backup/vendor_boot.img   # recon
shim/build-pixel7.sh backup/boot.img out/payload.bin
# -> shim/out/pixel7/pixel7-boot.img  (our Image as kernel, payload as ramdisk, v4 sig dropped)
```

Sanity: the repack prints `kernel <old> -> <new>, ramdisk <old> -> <new> bytes (header v4)`.
`fastboot boot` never writes anything — stock Android is untouched (Power+Vol- to reboot if it
sits at the splash).

## 2. Boot and read the outcome

```sh
adb reboot bootloader
fastboot boot shim/out/pixel7/pixel7-boot.img
```

The shim writes to two sinks before anything else: the ABL splash framebuffer (if the DTB carried
a `framebuffer`/`splash` reserved-memory node) and the gs201 ramoops console zone
(`ramoops@0xfd3ff000`, 2 MiB console first — works even with a black screen). Read it back after
any reset into Android:

```sh
adb root && adb shell cat /sys/fs/pstore/console-ramoops-0 | head -30
```

Expected first lines (shim/pixel7.c): `PIXEL7 SHIM: loader`, `DTB`, `CurrentEL`, `MPIDR`,
`CNTFRQ` (0x1770000 = 24.576 MHz), `MMFR0`, `ramoops`, `FB`, then the loader's payload hunt.

## 3. Outcome table

| You see / read | Meaning | Fix |
| --- | --- | --- |
| `LOADER: no initrd in /chosen` | ABL didn't map the boot ramdisk to `linux,initrd-*` (some ABL builds only do this for vendor_boot ramdisks) | move the payload into the vendor_boot vendor ramdisk instead — repack tooling already splits/merges fragments |
| `LOADER: no SWEETXNU payload` | initrd found but wrong content | check the repack ramdisk byte-compare (selftest) |
| Screen stays black, pstore has logs | framebuffer node missing/renamed on this ABL build | grep the dumped DTB (`scripts/fdtcat.py dtb/boot_unpacked/dtb`) for `reserved-memory` children; add the name to the shim's fallbacks |
| pstore empty, no screen | shim died before plog_init (exception) or wrong EL | if `CurrentEL` shows EL2 entry, start.S needs an EL2→EL1 drop (gs201 pKVM-era ABL) — the log tells you which |
| `EXC slot` in pstore | exception in the shim (bad address, alignment) | ESR/FAR/ELR lines identify the faulting access |
| `JUMP` then nothing | xnu entered, died before its own fbcon/pstore init | xnu sweet_early's log zone takes over from here (upstream debug flow) |

## 4. After the stub boots

Replace the stub payload with a real SWEETXNU payload (`tools/mkpayload.py`, PIXEL7 dict in
mkadt — §8 of PIXEL7-PORT.md has the confirmed gs201 values; only `mem_size` + live `fb` wait on
the M1 pack). Keep `phys_base` inside 0x80000000–0x8fffffff. From here the milestones are
PIXEL7-PORT §4 (M2 → early xnu print, M3 → SMP/ACPM, M4 → UFS + iOS root).
