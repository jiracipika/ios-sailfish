# gs201 (Tensor G2, Pixel 7 "panther") platform notes

All values read from Google's own gs201 kernel tree (`research/kernel-gs`,
`arch/arm64/boot/dts/google/gs201*.dtsi`) — these are the numbers mkadt/PIXEL7 and the
kernel patches need. Files of record: `gs201.dtsi`, `gs201-cpu.dtsi`, `gs201-ufs.dtsi`,
`gs201-pcie.dtsi`, `gs201-rmem.dtsi`, `gs201-drm-dpu.dtsi`.

## CPUs (gs201-cpu.dtsi)

| Logical | MPIDR | Cluster | capacity |
| --- | --- | --- | --- |
| cpu0–3 | 0x000–0x300 | little (A55) | 250 |
| cpu4–5 | 0x400–0x500 | mid (A76) | 927 |
| cpu6–7 | 0x600–0x700 | big (X1) | 1024 |

- Boot protocol: **PSCI 1.0, method smc** (`gs201-cpu.dtsi:163`) — the shim's PSCI CPU_ON/
  SYSTEM_RESET calls are correct for gs201.
- Android bootargs confirm earlycon: `earlycon=exynos4210,mmio32,0x10A00000
  console=ttySAC0,115200n8` and `firmware_class.path=/vendor/firmware` (Wi-Fi/BT blobs live there).

## Interrupt controller + timers (gs201.dtsi)

- GICv3 (GIC-600 class): **GICD 0x10400000 (64 KiB), GICR 0x10440000 (1 MiB, 8 cores)**.
- Arch timer: **counter frequency 24.576 MHz** (`clock-frequency = <24576000>`), PPIs 13/14/11/10
  standard. XNU's timebase and the shim's delay_ms (cntfrq_el0) must expect 24.576 MHz,
  not Qualcomm's 19.2.
- SPI numbering: gs201 uses its own `IRQ_*` map (`include/dt-bindings/interrupt-controller/gs201.h`;
  e.g. `IRQ_PCIE_GEN4A_1_HSI2 = 428` → GIC SPI 428).

## Debug UART (gs201.dtsi:193)

- `serial_0` = Exynos **USI** UART @ **0x10A00000**, 115200 8N1, 256-byte FIFO, needs
  USI config (samsung,usi-serial-v2) + PERIC0 clock gates via CMU — for a bare-metal bring-up
  log channel this is the second console after the framebuffer.

## Power / system

- PMU system controller: **0x18060000** (power domains: e.g. HSI2 = pmu-offset 0x3ec4, see
  WIFI-BT doc).
- GPIO banks: GPIO_ALIVE **0x180D0000** (`pinctrl_0`); WLAN/BT pins live in pinctrl_1/pinctrl_7
  banks.
- ACPM (firmware mailbox for rails/clocks): acpm_mfd_bus0/1 **0x18100000 / 0x18110000** —
  the clock/voltage pathway that replaces Qualcomm RPM. M3 milestone gate.

## Storage (gs201-ufs.dtsi)

Exynos UFS (vs Qualcomm ufshc):
- HCI standard **0x14700000**, vendor ext **0x14701100**, UNIPRO **0x14780000**, protector
  **0x14600000**; DMA-coherent; PMD gear 4 / 2 lanes.

## Display (gs201-drm-dpu.dtsi)

- `drmdecon0` @ **0x1C240000** (+1 0x1C241000) — Exynos DPU/DECON family, panel DSI via the
  same block (panel details in `gs201-panther-display.dtsi`). The ABL splash framebuffer the
  shim reuses is carved by ABL at boot (found via the DTB `framebuffer` node).

## Reserved memory worth knowing (gs201-rmem.dtsi)

- ramoops **0xfd3ff000 (4 MiB)**, console-size 2 MiB at region start → shim pstore zone.
- abl_reserved **0xf8800000 (32 MiB)** — ABL's own playground; do not hand this to xnu.
- ect_binary 0x90000000, gsa 0x90200000, aoc 0x94000000 (48 MiB), tpu_fw 0x91E00000/0x93000000,
  gxp 0x98000000+ — **everything above 0x90000000 is a patchwork of firmware reservations**;
  the SWEETXNU payload phys_base stays in 0x80000000–0x8fffffff (see PIXEL7-PORT §4).
- Exact usable DRAM map (`memory@...` size) comes from the device DTB in the M1 data pack.

## vs Qualcomm (msm8996, upstream Pixel 1)

| Function | msm8996 (Pixel 1) | gs201 (Pixel 7) |
| --- | --- | --- |
| Page size | 4K | 16K native (xnu arm64 supports 16K: iPhone10,3 userland is 16K) |
| Clocks/regs agent | RPM (shared memory) | ACPM (mailbox @0x18100000) |
| PCIe | qcom pcie | samsung exynos-pcie-rc Gen4 (HSI1/HSI2) |
| UFS | qcom ufshc | samsung exynos-ufs |
| GPU | Adreno 530 (freedreno-able) | Mali G710 MP7 CSF (panthor-class, new work) |
| Display | MDP5 command-mode | Exynos DECON 0x1C240000 |
| Debug UART | qcom serial | USI 0x10A00000 |
| Counter freq | 19.2 MHz | 24.576 MHz |
