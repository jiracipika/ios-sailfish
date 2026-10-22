# Pixel 7 cellular — Exynos S5300 modem — research notes (M8)

Grounded in `research/kernel-gs/drivers/soc/google/cpif/` (63-file Samsung CP-interface stack,
in-tree) and `arch/arm64/boot/dts/google/gs201-cp-s5300-sit.dtsi` + `gs201-cloudripper-cp-s5300-sit.dtsi`.
**Expectation setting: this lane is research-grade** — the modem itself is a closed Shannon
firmware machine with signed boot images, NV/RF calibration from the factory, and an IPC protocol
we can read but never fully own. The realistic goal is modem bring-up to an IPC/AT channel, then
a minimal RIL replacement for SMS/calls. iOS's CommCenter/Telephony stack on top is its own
project (see PIXEL7-PORT §6).

## 1. Hardware/DTB facts

| Item | Value |
| --- | --- |
| Modem | Exynos S5300 (`mif,modem_type = <SEC_S5100>` — the S5300 rides the S5100 driver family) |
| Boot | SPI flash boot, `cpboot_spi` bus 9 (`compatible = "samsung,exynos-cp-spi"`) + memory boot |
| Main interface | shared memory: `samsung,exynos-cp-shmem` (SIT layout), IPC maps + RFS + dump |
| Devices | `/dev/umts_boot0`, `umts_ipc0`, `umts_rfs0`, `umts_bootdump0`, … (iod names in the dtsi) |
| In-tree stack | `drivers/soc/google/cpif/`: `modem_ctrl.c`, `link_device.c`, `boot_device_spi.c`, `s5100_sit.c`-family, `cp_btl`, `direct_dm`, netrx/qos/tp-monitor |

## 2. What the Android path does (readable in-tree)

1. Power + release: PMU/modem-ctl regs, then CP boot image streamed from SPI (or the `modemimg`
   partition for the main image) into CP SRAM/DRAM by the AP (`boot_device_spi.c`).
2. Handshake over shared memory: boot handshake → IPC map negotiation (`s5100_sit.c`).
3. IPC: SIT (Samsung IPC over shared memory) — the map/len/descriptor formats are fully visible
   in the driver; this is the real documentation of the protocol.
4. RFS (radio filesystem = NV-ish access) and dump channels; netdata path via `direct_dm`/netrx.

## 3. Why this is hard on an iOS-xnu host (honest ladder)

| Step | Work | Exit criteria | Risk |
| --- | --- | --- | --- |
| M8.1 | SPI/power bring-up + boot the stock `modemimg` blob, watch the boot handshake | `boot0` handshake completes, CP alive | signed-image chain must come from the phone (extract, never commit); a failed auth bricks nothing but no boot |
| M8.2 | Shared-memory IPC: parse the SIT map, echo one request | valid IPC exchange in shmem | format drift between CP firmware builds |
| M8.3 | Minimal AT/SIT channel: SMS-send + signal read | raw SMS out, RSSI in | needs NV present (factory data from the phone) |
| M8.4 | RIL-replacement daemon (userspace on xnu) + audio PA path | voice call | audio codec/PA routing is its own ASoC-shaped job; worst risk of the lane |

Nothing here touches xnu core design — it's a device driver + a userspace daemon. But M8.4's
audio routing (Exynos ABOX) is a second bring-up-scale effort; expect cellular to trail WiFi by
months, and treat "texting works" as the realistic finish line for this lane.

## 4. Firmware policy

`modemimg`/CP SPI images, NV data, RF cal: extract from the device (M1 pack), never commit.
The cpif driver source itself (GPL, in Google's tree) is our protocol documentation and may be
quoted into the repo as needed.
