# Pixel 7 (panther, gs201) port plan

Status: planning. Fork: [jiracipika/ios-sailfish](https://github.com/jiracipika/ios-sailfish),
forked from [joranv1/ios-sailfish](https://github.com/joranv1/ios-sailfish) (Pixel 1 / msm8996),
which forks [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) (Redmi Note 10 Pro / SM7150).
Read the root README first — it explains the boot path, the iOS root build and the kernel layout, all of
which this plan inherits.

## 0. Reality check (read this before anything else)

The upstream projects have **never had Wi-Fi, Bluetooth or cellular on any device** — not "broken",
simply not brought up yet. So the goals map to:

| Goal | Truth |
| --- | --- |
| "Fix" GPU acceleration | New bring-up. Pixel 1's working GPU driver is for the **Adreno 530** (freedreno). Pixel 7 has a **Mali-G710 MP7 (CSF)** — a different GPU, different userspace stack (panfrost/panvk instead of freedreno). The *pattern* (Mesa behind the `sweetgl.dylib` EAGL shim) transfers; the driver does not. |
| "Fix" Wi-Fi / Bluetooth | New bring-up, on both Pixel 1 and Pixel 7. Nobody in this family has done radios yet. |
| Calls and texts | The hardest thing in this plan. iOS's CommCenter speaks Apple's baseband protocol; the Pixel 7 has an Exynos 5300 modem with none of the reverse-engineered plumbing iOS needs. Research-grade, months, unknown outcome. Do not plan your daily phone around this. |
| iMessage later | See §7. Not a GenSMBIOS-style config problem; needs pypush-style IDS/activation RE, and it is a ToS gray zone. |

A realistic ordering is: **boot → display/touch (CPU-rendered SpringBoard, like ios-sweet today) →
UFS/root polish → GPU → Wi-Fi/BT at kernel level → cellular (maybe never)**. That ladder mirrors how
both upstream ports actually progressed.

## 1. Why the Pixel 7 is a good target

- **16K pages** — the gs201 Android kernel itself runs `PAGE_SIZE=16384`, exactly what this XNU port
  requires. (Upstream even added `tools/granule-probe` for this.)
- **ARMv8.2 cores** — the Pixel 7 can run the **iPhone X (iPhone10,3 / D22AP) userland** that ios-sweet
  uses, instead of the Pixel 1's ARMv8.0-compromise iPhone 8 userland. Gesture navigation, no Home
  button — matches the phone's shape. `tools/mkadt.py` already builds the D22AP tree by default.
- **8 GB RAM** vs the Pixel 1's 2 GB — iOS 16 is comfortable.
- **`fastboot boot` works on an unlocked Pixel 7** — same non-destructive boot path (nothing flashed).
- **Google publishes everything** (GPL): `android.googlesource.com/kernel/gs` (pantah branch,
  android13-5.10 + vendor modules) and `device/google/pantah` (DTS). Most bring-up info is *reading
  published source*, not black-box RE. This is a huge advantage over both upstream targets.
- GKI boot images (boot v4): `tools/unpack_bootv2.py` handles v0–v4 + vendor_boot v4 (self-tested, `--selftest`).

## 2. Hardware delta: msm8996 (Pixel 1) → gs201 (Pixel 7)

Everything below the kernel must be replaced or re-verified. Per-area:

| Area | Pixel 1 (sailfish) — done upstream | Pixel 7 (panther) — needed |
| --- | --- | --- |
| CPU | 2×1.6 + 2×2.15 GHz Kryo (ARMv8.0), PSCI bring-up, patches 0085/0088/0093/0102 | 2×X1 (2.85) + 2×A78 (2.35) + 4×A55 (1.8). Patch `0078-sweet-smp-8-cores` upstream already does 8 cores (for sweet). PSCI presence at EL3 to be verified on gs201 (Google's firmware); clock control later via ACPM, not RPM |
| Interrupts | GIC-v3 with a non-standard redistributor stride (patch 0079) | GIC-600; addresses/stride from the stock DTB (`gic@10400000`-region in gs201 DTS) |
| Memory | upper DDR bank, ~2 GB, XPU carve-outs skirted | 8 GB; take base/size + reserved carve-outs from the stock FDT; watch the HYP/hypervisor and S2MPU memory-permission ranges (gs201 ships a hypervisor + S2MPU MMIO/memory firewall — first-boot experiment territory) |
| Storage | Qualcomm ufshc (patch 0028/0030/0034/0069) | **Exynos UFS host + UNIPRO** (`ufs-exynos` in kernel/gs) — rewrite `sweet_ufs.c` against a different controller |
| Display | MDP5 command-mode panel, kickoff reg, 60 Hz (patches 0087/0099/0103/0164/0167/0168) | Exynos **DPU (decon) + DSIM**, Samsung `s6e3hc3-c10` 1080×2400 90 Hz AMOLED, likely video-mode (self-refreshing — *simpler* than Pixel 1: no kickoff reg; brightness stays DCS 0x51 path). ABL splash leaves the DPU running for the shim console, same trick as Pixel 1 |
| Touch | Synaptics RMI4 over BLSP I²C, rails via RPM (patches 0096–0100) | Goodix over USI (HSI2C) — exact part from `device/google/pantah` DTS; rails via ACPM/PMIC (s2mpg12/13). Same HID digitizer event path |
| Buttons | PMIC/TLMM via SPMI | s2mpg12 PMIC / TEGRA? no — GPIO + PMIC keys via SPMI-equivalent (exynos SPMI present on gs201); HID path identical |
| Battery/charge | PMI8994 fuel gauge + 1.5 A charger (patches 0114–0118) | max77759 fuel gauge / s2mpg12 charger — new driver, same IOPMPowerSource plumbing |
| Power/clock firmware | RPM over GLINK (`sweet_rpm.c`) | **ACPM (ACTLM)** mailbox firmware — Google's `drivers/soc/google/acpm*`; this is the backbone every rail/clock votes through. Biggest single kernel-side subsystem to port |
| GPU | Adreno 530: SMMU, CP microcode, zap shader via TrustZone; compute + quads + Mesa freedreno behind EAGL (patches 0119–0164, tools/sweetgl) | **Mali-G710 MP7 (Valhall 3rd-gen CSF)**: MMU-600 SMMU, CSF firmware blob (`mali_csffw.bin` from vendor image), CSF queue interface — essentially a XNU port of the `panthor` kernel driver; userspace = Mesa **panfrost** (GLES on panthor, supports CSF GPUs in recent Mesa) swapped into `tools/sweetgl` in place of freedreno. Verify the exact Mesa version needed for G710/CSF GLES during bring-up |
| USB | Device mode + ECM network (patches 0072–0076) — gives kernel networking + logs over USB | gs201: Exynos DWC3 — different controller, same idea. Early priority: this is the debug lifeline the Pixel 1 port lacked early on |
| Wi-Fi/BT | none | Broadcom **BCM4389** Wi-Fi 6E/BT5 combo on **PCIe** (see §5) |
| Modem | none (MSM8996 has one; unused) | Exynos 5300 (see §6) |

## 3. What transfers as-is

The genuinely reusable ~70% of the repo:

- `shim/` loader/MMU/pstore/FDT machinery and `tools/mkpayload.py` (payload packing) — adapt entry
  addresses and the watchdog to gs201.
- `tools/mkadt.py` framework + **the whole identity-spoofing story**: synthetic ECID, `/chosen`
  security-status → mobileactivationd "Hactivation" without Apple servers, `/product` from the real
  IPSW device tree, synthetic MACs in `/chosen` (MobileGestalt reads them first — this already
  prevents hangs upstream).
- iOS root build: `tools/mkiosroot*.sh`, `tools/sweet-root-tweaks.py`, `tools/wallpaper-heic2png.sh`,
  iPhone10,3 IPSW choice (as above).
- XNU patches `0001–0078` (SoC-generic: build fixes, pmap, AMFI permissiveness, HFS, IOSurface,
  IOMobileFramebuffer shim, HID, USB ECM, 8-core SMP).
- CPU-rendered display path (`SweetSurface/SweetFramebuffer/AppleCLCD`) — Pixel 7 boots exactly like
  ios-sweet does today: iOS sees an iPhone X panel, CoreAnimation renders in software, the kernel
  copies into the DPU's scanout buffer. GPU comes *after* the phone boots to the home screen.
- `tools/sweetgl` **as a pattern**: EAGL shim + CoreAnimation GL-function shims (the missing-texture-
  function fixes are gold) with a different Mesa driver underneath.
- CI: `.github/workflows/xnu.yml` builds the kernel on GitHub macOS runners — fork-friendly.

## 4. Milestones

| # | Milestone | Exit criteria |
| --- | --- | --- |
| M0 | Kernel builds locally on this Mac | `tools/local-toolchain.sh` green (Xcode 15.4 + KDK 13.5 in `research/`); `tools/xnu-build-local.sh` produces a kernel artifact (see docs/LOCAL-BUILD.md; GitHub Actions is the fallback path, not the plan) |
| M1 | Device data pack | Stock `boot.img`/`dtbo` pulled + unpacked; gs201 FDT values extracted (memory, GIC, UFS reg, CPU MPIDRs, panel/touch nodes); `PIXEL7` dict drafted in `mkadt.py` (template in §8); iPhone10,3 device tree extracted from IPSW |
| M2 | XNU early boot on the phone | `fastboot boot` → shim → XNU prints to the ABL framebuffer + pstore ring; GIC-600 IRQs + virtual timer alive; readable from Android (`pstore`) |
| M3 | 8 cores + ACPM | SMP up (2 big + 2 mid + 4 little); ACPM mailbox talks; one rail/clock voted through it |
| M4 | Storage + root | Exynos UFS driver; iOS 16 root mounts read-write; launchd → backboardd → **SpringBoard on the Pixel 7's own panel, CPU-rendered**, touch working — ios-sweet's current state, on Pixel 7 hardware |
| M5 | Daily-usable sans radios | USB ECM networking, brightness/sleep (DCS 0x51), battery %, buttons — the Pixel 1 fork's current state, minus GPU |
| M6 | GPU | Mali CSF bring-up ladder mirroring upstream's 0119→0164 arc: power/clocks → SMMU → FW download → queue → compute shader → quad → present; panfrost-in-sweetgl last |
| M7 | Wi-Fi/BT kernel-level | PCIe link to BCM4389, firmware download, CLM, packets through a BSD interface (kernel ping); BT HCI over UART (HCI version readback) |
| M8 | Cellular / iMessage research | Separate, long-horizon; see §6–7 |

M2–M4 is the scale of work upstream spent ~90 patches and many months on for two different phones;
expect the same order of magnitude. Everything is `fastboot boot`-only — stock Android stays
flashed and boots normally otherwise.

The M2 shim exists (differs from the Pixel 1 flow in three ways): the payload rides as the
boot.img **ramdisk** (ABL patches `/chosen linux,initrd-start/end`, no LK `_end` append), the
splash framebuffer + ramoops console zone are **discovered from the DTB** (`shim/fdt.c`
`fdt_find_reserved` — ABL adds the framebuffer node at boot; gs201 ramoops is `ramoops_mem@fd3ff000`,
console-size 2 MiB first, fallback hardcoded), and there are no MMIO pokes (gs201 watchdog map
unverified — a hang needs a manual reset until then). Build:
`shim/build-pixel7.sh STOCK_BOOT.img [PAYLOAD]` → `shim/out/pixel7/pixel7-boot.img` (NDK clang
auto-detected under `/Volumes/HARD/android-sdk`; GNU/aarch64-elf prefixes also work). Host tests:
`shim/host_fdt_test.c` (FDT walkers) and `tools/unpack_bootv2.py --selftest` (image round-trips).
One constraint for later: the payload's `phys_base` must stay inside 0x80000000–0x8fffffff until
mkpayload learns the gs201 reserved-memory map (ect/gsa/aoc/abl/ramoops all sit above 0x90000000).

## 5. Wi-Fi / Bluetooth (BCM4389)

- The combo sits on PCIe (`wlan` under `pcie0` in the gs201 DTS). Google's kernel ships the Android
  **bcmdhd** FullMAC driver; upstream Linux uses **brcmfmac** for the same firmware family. Either
  firmware protocol is a candidate; brcmfmac's PCIe bus code is the better-documented port target.
- Firmware/CLM/NVRAM come from the phone's own vendor image (`/vendor/dlkm`/`/vendor/firmware`,
  `brcm/bcm4389c1*` files) — extract, never commit (same policy as upstream's Adreno blobs).
- On XNU there is **no open 80211 family** — Apple's `IO80211Family`/`AppleBCMWLAN` are closed and live
  in the (still encrypted?) iPhone kernelcache. Two tracks:
  1. *Kernel-level* (do first): PCIe link → firmware download → a minimal XNU BSD `ifnet` driver
     speaking enough of the firmware protocol to associate + pass frames. XNU ships the full BSD
     network stack, so ping/TCP from kernel or a console tool is a real, demoable milestone.
  2. *Userland*: making iOS's Wi-Fi Settings/WiFiManager work needs an IO80211-compatible kext or
     daemon shims — research; only start after track 1.
- Bluetooth: bring the combo's BT side up over UART HCI first (version readback = milestone), UI-level
  BT needs Apple's closed IOBluetoothFamily — same later-research caveat.

## 6. Calls and texts

iOS's CommCenter/basebandd speak Apple's proprietary baseband interfaces (Qualcomm MDM on old
iPhones). The Pixel 7's Exynos 5300 modem has an Android rild/vendor stack we'd have to RE far
enough to drive SMS/calls from a custom kernel side. Nothing in the upstream family has touched
cellular. Treat as open research: useful first steps are logging the vendor RIL traffic on stock
Android (RIL proxy) and studying how Corellium-style projects fake baseband for CommCenter. Set
expectations accordingly.

## 7. Identity spoofing (the "GenSMBIOS" question)

**Already built, and it works differently than Hackintosh.** `tools/mkadt.py` is this project's
GenSMBIOS: it fabricates the Apple Device Tree that XNU/iOS believe — product type (`iPhone10,3`),
synthetic ECID, `/product` copied from the real iPhone IPSW tree (MobileGestalt's source of truth),
security-status properties that steer `mobileactivationd` into its native "Hactivation" path
(activated without any Apple server), synthetic Wi-Fi/BT MACs. iOS fully believes it is an iPhone.
What it is *not* is a real activation: no SEP, no data protection, security mostly off.

**iMessage/FaceTime is a different animal.** Hackintosh iMessage works because macOS + valid
serial/ROM/MLB on Apple's boot architecture passes server checks. Here the device would have to
register with Apple's IDS as an iPhone — no real activation record, no APNS certs. The relevant prior
art is **[pypush](https://github.com/JacobCoxison/pypush)** (jkcoxson), which RE'd Apple's
IDS registration/activation to get iMessage on non-Apple devices in 2023 (Apple patched the hole;
forks/experiments continue). That's the roadmap if we ever go there — and it's a ToS gray zone that
can flag a real Apple ID. Park it; the "usable phone" goals don't depend on it.

## 8. mkadt PIXEL7 dict — template

Values come from the stock DTB (M1 extraction; every address is in Google's published DTS — do not
guess from memory):

```python
# tools/mkadt.py  (draft — fill ONLY from device/google/pantah + your live FDT)
PIXEL7 = dict(
    phys_base=...,        # lowest usable DRAM (skip HYP/S2MPU carve-outs)
    mem_size=...,         # from /memory + reserved-memory of stock FDT
    gicd=..., gicr=..., gicr_stride=0x10000,   # GIC-600 (verify stride!)
    vtimer=...,           # PPI numbering from boot probe
    fb=..., fb_size=..., fb_w=1080, fb_h=2400, fb_bpp=32,  # ABL splash scanout
    ufs=..., ufs_size=...,                     # ufshc reg (exynos)
    cpus=[(mpidr, cluster, max_hz) × 8],        # 2×X1, 2×A78, 4×A55 — MPIDRs from FDT
)
```

Presented identity: D22AP/iPhone10,3 (mkadt's default), display-scale 3, canvas 1125×2436, sRGB gamut
tweak likely needed like the Pixel 1 needed it (see the `pixel1` branch of `build()`).

## 9. What to extract (device + IPSW)

Run `tools/pixel7-extract.sh` for the guided checklist. Summary:

- From the phone (stock Android, root or TWRP-ish recovery): `boot.img`, `dtbo.img`, `vbmeta.img`,
  `vendor_boot.img`; from `/vendor`: `dlkm` Wi-Fi firmware/NVRAM/CLM, `mali_csffw.bin` (+ firmware
  dir), modem files (for the far-future M8), panel/touch dtbo fragments. Keep out of git
  (`.gitignore` already covers `backup/`, `research/`).
- From Apple: iPhone10,3 16.7.16 (20H392) IPSW — extract `DeviceTree.d22ap.im4p` only
  (`tools/remotezip.py`), same as ios-sweet's recipe.
- Public sources to keep open beside the code: `kernel/gs` (pantah android13-5.10 branch + vendor
  modules: `acpm`, `ufs-exynos`, `decon`/`dsim`, `dwc3-exynos`, `exynos-pcie`, `s2mpu`, PMIC),
  `device/google/pantah` (DTS), Mesa panfrost/panthor, brcmfmac PCIe docs.

## 10. References

- Upstream: [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) ·
  [joranv1/ios-sailfish](https://github.com/joranv1/ios-sailfish) ·
  [MakrSas/ios-topaz](https://github.com/MakrSas/ios-topaz)
- Communities: r/hackios (ios-sweet thread) · r/hackintosh (sailfish thread)
- Kernel/DT: [Build Pixel kernels](https://source.android.com/docs/core/architecture/kernel) ·
  android.googlesource.com `kernel/gs` + `device/google/pantah`
- GPU: [Panthor announcement (Collabora)](https://www.collabora.com/news-and-blog/news-and-events/announcing-panthor.html) · Mesa panfrost/panvk
- Wi-Fi: [Broadcom BCM4389](https://www.broadcom.com/products/wireless/wireless-lan-bluetooth/bcm4389) · linux brcmfmac docs
- iMessage RE: pypush (jkcoxson) and descendants
