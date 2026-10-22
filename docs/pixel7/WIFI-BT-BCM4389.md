# Pixel 7 Wi-Fi / Bluetooth (BCM4389) — bring-up notes

Grounded in the downloaded sources (see `research/`): `kernel-gs` (Google's gs201 kernel,
`arch/arm64/boot/dts/google/gs201-*.dtsi`), `bcmdhd4389` (Google's out-of-tree DHD driver),
`bt-broadcom/nitrous.c` (Google's BT power/wake driver). Everything below cites file + line-level
facts, not guesses. Upstream ios-sailfish has NO Wi-Fi at all — this is new bring-up (M7).

## 1. Hardware map (from the gs201 device tree)

The combo sits on **pcie_1** = HSI2 block, Exynos Gen4 RC instance 1
(`gs201-pcie.dtsi`, wired in `gs201-cloudripper-wlan.dtsi`):

| Item | Value |
| --- | --- |
| RC elbi | 0x14520000 (32 KiB) |
| RC phy (gen4) | 0x14550000 (32 KiB), pcs 0x14540000 (4 KiB) |
| sysreg | 0x14420000 (32 KiB) |
| DBI | 0x14800000 (0x301000 — DBI + ECAM-ish config mirror) |
| config space (ATU-mapped ECAM window) | 0x60FFE000 (8 KiB) |
| I/A (item/address translation) | 0x14500000 (unused: `use-ia=false`) |
| MEM ranges (outbound) | 0x60000000 + 0xFF0000 (non-prefetch 32-bit) |
| PERST# GPIO | gph2-0 |
| max-link-speed | Gen3, num-lanes 2, ch-num 1 |
| PMU power-domain offset | 0x3ec4 (`pmu_system_controller` + `pmu-offset`) |
| RC IRQ (gs201 SPI) | 428 `IRQ_PCIE_GEN4A_1_HSI2` (+ 4 MSI lines; `use-msi=false` on Android — DHD is polled/OOB) |
| coherent | pcie_1 node gets `dma-coherent` + `use-cache-coherency=true` from the wlan overlay |

WLAN child of pcie_1 (`gs201-cloudripper-wlan.dtsi`):

| Signal | GPIO | Role |
| --- | --- | --- |
| `wl_reg_on` | **gph2-4** | WLAN_REG_ON — powers the combo's Wi-Fi half |
| `wl_host_wake` | **gpa6-1** | OOB interrupt chip→host (DHD `dhd_oob_irq`) |
| `wl_dev_wake` | **gph2-5** | host→chip wake |

Device identity: PCI vendor 0x14e4, device **0x4441** (`BCM4389_D11AX_ID`,
`bcmdhd4389/include/bcmdevs.h:138`); chipcommon ID 0x4389. BT is the same chip's second
function, but on Pixels it hangs off a **UART**, not PCIe (see §5).

Power sequencing reality: HSI2 power domain must be on **before** any RC register touch
(PMU offset 0x3ec4), then REG_ON (gph2-4) high, then PERST# (gph2-0) release, then link
training. On Android the clock gates flow through ACPM/CMU, not direct MMIO — on a bare
xnu the RC bring-up must replicate what `pcie-exynos-gs101-rc.c` does incl. its PHY cal path.

## 2. Driver-side facts (bcmdhd4389)

- Build: `CONFIG_BCM4389=y` (`Kbuild:47,914` → `-DBCM4389_CHIP_DEF -DSUPPORT_MULTIPLE_REVISION...`).
- Firmware: `VENDOR_PATH + CONFIG_BCMDHD_FW_PATH` (`dhd_linux.c:9615`) — i.e. files live in the
  phone's `/vendor`, NOT in the kernel tree: extract `brcm/bcm4389c1*/` + NVRAM + CLM from the
  stock image (`/vendor/dlkm/lib/modules`, `/vendor/firmware/brcm/`). Never commit (same policy
  as upstream's Adreno blobs).
- Bus: PCIe with DHD's ring/memwindow protocol (like brcmfmac's PCIegen: host-side windowed
  writes into chip SRAM for the FW image, then doorbells). `dhd_pcie.c` in this tree is the
  reference; brcmfmac `pcie.c` implements the same class of protocol against the same firmware
  family and is far smaller.
- OOB: interrupt via `wl_host_wake` (gpa6-1), matching upstream xnu work: one GIC SPI → IOFilterInterruptEventSource.

## 3. XNU port ladder (M7, concrete)

1. **M7.0 — pins + power**: PPMU/PMU domain on (0x3ec4 bit), gph2-4 high, 50 ms, PERST low→high.
   GPIO block: same pinctrl banks Linux uses (gph2 = bank at? read from the pinctrl dtsi when we
   extract the real DTB — do NOT guess addresses; the device data pack (M1) gives them).
2. **M7.1 — RC init, no link**: port `pci-exynos.c`/`pcie-exynos-gs101-rc.c` essentials: elbi/dbi
   map, PHY init (`pcie-exynos-gs101-rc-cal.c` — the cal/retake sequence), sysreg tweak, ATU
   outbound for 0x60000000. Success metric: DBI reads back PCIe caps sane.
3. **M7.2 — link + config space**: release PERST, poll LSTS/LTSSM until L2/L3 trained (Gen3 x2),
   ECAM window 0x60FFE000 → read vendor/device: expect **14e4:4441**. That single readback is the
   "PCIe works" gate; everything else is protocol.
4. **M7.3 — firmware download**: windowed writes over BAR0 (DBI-mapped) into chip RAM: CLM blob,
   then FW image, then NVRAM text; doorbell; wait for DHD console "PCS" log in chip SRAM.
   Reference: brcmfmac `brcmf_pcie_download_fw_nvram` (~200 lines) rather than DHD's.
5. **M7.4 — BSD ifnet**: minimal `ifnet` (Ethernet-family stub is enough to carry 802.11 frames to
   userspace later): DHD ring buffers (shared-memory rings like brcmfmac pcie), OOB IRQ →
   IOInterruptEventSource, `dlil` attach. Milestone: `ping` from a kernel console tool through
   associated AP (WPA2 handled by firmware for open/WPA-PSK via wl iovars).
6. **M7.5 — real 802.11**: wpa_supplicant-equivalent in userspace later; first pass pins
   PSK association in-kernel via iovars (same trick DHD uses on Android for P2P/softap).

## 4. Firmware-protocol strategy

Two candidates, same firmware family:

- **brcmfmac PCIe** (upstream Linux `drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c`):
  small, well-commented, matches the 4389's FW ABI generation. **Port target.** Needs FW/CLM/NVRAM
  extracted from the phone (BCM4389 FW is not in linux-firmware).
- **bcmdhd** (this tree): complete but Android-shaped ( Binder-era ioctls, dhd_cfg80211 layers);
  use it as the protocol dictionary (iov names, ring layout), not as porting source.

## 5. Bluetooth (same chip, different bus)

`bt-broadcom/nitrous.c` + `nitrous.txt`: BT is a **Broadcom controller over UART** with:
- `power-gpio` (BT_REG_ON), `host-wake-gpio` (BT chip→host), `dev-wake-gpio` (host→chip),
- rfkill + serial-core PM. On pantah the GPIOs live in the same pinctrl banks as WLAN's
  (extract exact pins from the device DTB in M1).
- XNU path: there is no UART driver yet upstream in ios-sailfish (Pixel 1 has none either) —
  BT ranks behind Wi-Fi; the honest ladder is: serial driver (Exynos UART @?, from DTB) →
  HCI H4 → Broadcom `bcm` line-discipline init (baud bump + patchram download) →
  HCI ACL/SCO on an XNU `*` transport. Park until Wi-Fi M7.3 is green.

## 6. What we still need (device data pack, M1)

1. Stock DTB (vendor_boot) — exact pinctrl bank addresses, PCIe clocks/cmu names, PMU layout.
2. `/vendor/firmware/brcm/*4389*` + CLM + NVRAM text (and BT `*.hcd`/patchram).
3. Boot-time ABL log (`/proc/last_kmsg` era → `pstore`) showing the PCIe link training lines
   (`exynos-pcie` prints) — confirms the expected LTSSM sequence on real silicon.

None of this blocks the M7.1/M7.2 skeleton: addresses above are from Google's own DTS.
