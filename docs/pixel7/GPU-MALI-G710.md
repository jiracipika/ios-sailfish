# Pixel 7 GPU — Mali-G710 MP7 (Valhall/CSF) — bring-up notes

Grounded in `research/kernel-gs/arch/arm64/boot/dts/google/gs201-gpu.dtsi` and the downloaded
mainline references `research/panthor-src/` (DRM panthor, the upstream Mali CSF kernel driver) and
`research/mesa-pan/` (Mesa panvk userspace: `panthor_kmod.h` UAPI, `pan_cs.c` command-stream
builder, `v10.xml` ISA). This is M6; upstream ios-sailfish's GPU arc (patches 0119–0168) was
**Adreno 530** — none of those registers transfer, only the *ladder shape* and the sweetgl
EAGL-shim pattern.

## 1. Hardware map (gs201-gpu.dtsi — hard facts)

| Item | Value |
| --- | --- |
| Register block | `mali@0x28000000`, 16 MiB (`compatible = "arm,malit6xx"`) |
| IRQs (gs201 SPI) | GPU **328**, JOB **329**, MMU **330** (CSF needs GPU + MMU; JOB is legacy-named) |
| Power | two domains: `pd_g3d` (top) + `pd_embedded_g3d` (cores), PMU status @ offset **0x2004** |
| Clocks | **through ACPM** (`gpu0_cmu_cal_id = ACPM_DVFS_G3DL2`, `gpu1 = ACPM_DVFS_G3D`) — no direct CMU pokes; same story as every gs201 clock/rail |
| DVFS range | 202–996 MHz (L2/shader pairs in the DTS table) |

Google's DTS carries Android security glue we do NOT port: `physical-memory-group-manager`,
`priority-control-manager`, `protected-memory-allocator` (KNOX-style protected GPU memory).

Note: **the Mali kernel driver is not in this kernel tree** — Google ships it out-of-tree
(Arm mali_kbase lineage, which is what `arm,malit6xx` matches). The two open references are on
disk: panthor (kernel, ~12 kLOC, mainline since 6.3 for Valhall-CSF: G610/G710 family) and
Mesa panvk (userspace). G710 = Valhall gen 2 **CSF** (command-stream frontend, on-chip MCU
firmware) — like Adreno, the GPU runs firmware we must load.

## 2. What maps to xnu

panthor decomposes into exactly the pieces a bare-metal xnu port needs (file → role):

| panthor file | Role | xnu counterpart |
| --- | --- | --- |
| `panthor_gpu.c` / `panthor_gpu_regs.h` | power-on L2, idle waits, GPU_ID/ARCH readback | sweet_early-style MMIO driver |
| `panthor_fw.c` / `panthor_fw.h` / `panthor_fw_regs.h` | `mali_csffw.bin` load into chip SRAM, GLB interface, doorbells | the heart of the port |
| `panthor_mmu.c` / `panthor_mmu_regs.h` | Mali MMU: 16 ASs, 4-level PTs, AS_COMMAND | xnu has no IOMMU framework — small custom mapper over GPU VA space |
| `panthor_sched.c` | CSF CSG/CS queue pairs, scheduling | minimal: one CSG, one CS queue |
| `panthor_bo.c` / `panthor_gem.c` | buffer objects | xnu IOBufferAllocator |
| `panthor_drm.h` + `mesa-pan/panthor_kmod.h` | UAPI surface | becomes the sweetgl driver-user boundary |

## 3. Milestone ladder (maps onto upstream's Adreno arc)

| Step | Upstream analog | Work | Exit criteria |
| --- | --- | --- | --- |
| M6.1 | 0119–0121 (power/clocks) | PMU domains on (0x2004 mask 0x1), ACPM vote G3D frequency | GPU_ID @ 0x28000000 reads back Valhall arch + 7 cores |
| M6.2 | 0122+ (RPM/fw) | extract `mali_csffw.bin` from the phone (`/vendor/firmware`), map into chip SRAM, kick MCU, doorbell | firmware VERSION + feature masks read back; doorbell advances FW counters |
| M6.3 | 012x SMMU | Mali MMU: build one 4-level page table, AS_ALLOCATE + AS_COMMAND UPDATE, GPU-side readback of a host-written page | GPU reads what CPU wrote through an AS |
| M6.4 | ringbuffer patches | CSF: GLB create CSG, one CS queue pair (_ring + profiling), doorbell | FW reports the queue ACTIVE |
| M6.5 | 015x compute | command stream (Mesa `pan_cs.c` format) running a trivial v10 compute shader with WRITE_VALUE | shader result lands in host memory |
| M6.6 | 016x present | tiler + fragment CS rendering a quad into a DECON-scannout buffer | quad on screen; then the sweetgl EAGL shim maps onto this exactly like Adreno's did |

## 4. Firmware policy

`mali_csffw.bin` (G710, v10) comes from the phone's `/vendor/firmware` (data pack, M1) —
extract, never commit (same policy as upstream's Adreno/Zap blobs). CLM/NVRAM analogues do not
exist for Mali; it's a single blob.

## 5. Why panthor and not Google's mali_kbase

kbase is Android-coupled (ION/dmabuf/PGM hooks, its own UAPI, ~5× the code) and not in the tree
anyway; panthor is mainline, documented, and the same "port the clean open driver, keep the blob"
pattern upstream used for Adreno/freedreno. Mesa panvk gives us the userspace command-stream
encoder so the xnu side never parses GL state — it consumes baked CS like upstream's Adreno path.
