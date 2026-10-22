/*
 * Pixel 7 (panther, Tensor gs201) shim. Screen: the ABL splash framebuffer,
 * reserved by ABL as a /reserved-memory child named framebuffer@... (or
 * splash@...) in the DTB it hands us; geometry fixed 1080x2400@32 like
 * mainline simplefb. Log: the gs201 ramoops console zone (kernel-gs
 * gs201-rmem.dtsi: ramoops_mem@fd3ff000, 4 MiB, console-size 2 MiB with no
 * record-size, so the console persistent_ram sits at the region start) --
 * same "DBGC" header layout as the Pixel 1 zone. After a warm reset read it
 * from Android: adb root; adb shell cat /sys/fs/pstore/console-ramoops-0
 *
 * Boot: tools/unpack_bootv2.py --repack puts this Image as the kernel of the
 * STOCK boot.img and the SWEETXNU payload as its ramdisk; nothing is flashed.
 * ABL loads vendor_boot's DTB, patches /chosen with linux,initrd-start/end
 * over the ramdisk, and jumps here with x0 = DTB. gs201 reserves big chunks
 * above 0x90000000 (ect/gsa/aoc/abl/ramoops...), so the payload's phys_base
 * must stay inside 0x80000000..0x8fffffff until mkadt/mkpayload learn the map.
 *
 * No MMIO pokes here: the gs201 watchdog map is unverified, so a hang ends
 * in a manual reset for now. If ABL enters at EL2 (gs201 pKVM-era boot),
 * start.S's EL1 vectors miss -- CurrentEL is logged for that diagnosis.
 */
#include <stdint.h>
#include "console.h"
#include "fdt.h"
#include "loader.h"
#include "pstore.h"

#define PSCI_SYSTEM_RESET	0x84000009UL
#define RAMOOPS_FALLBACK	0xfd3ff000UL	/* gs201-rmem.dtsi ramoops_mem */

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static void __attribute__((noreturn)) psci_reset(void)
{
	register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_RESET;
	__asm__ volatile("dsb sy; smc #0" : "+r"(x0) : : "memory");
	for (;;)
		__asm__ volatile("wfe");
}

static void delay_ms(unsigned ms)
{
	uint64_t end = rsr("cntpct_el0") + rsr("cntfrq_el0") / 1000 * ms;
	while (rsr("cntpct_el0") < end)
		;
}

void shim_main(uint64_t dtb)
{
	/* Ramoops first: everything below must land in it. */
	uint64_t rz = RAMOOPS_FALLBACK, found = 0, found_sz = 0;
	if (fdt_find_reserved(dtb, "ramoops", &found, &found_sz) && found)
		rz = found;
	plog_init_at(rz);

	plog_str("PIXEL7 SHIM: loader\n");
	plog_hex("DTB", dtb);
	plog_hex("CurrentEL", rsr("CurrentEL"));
	plog_hex("MPIDR", rsr("mpidr_el1"));
	plog_hex("CNTFRQ", rsr("cntfrq_el0"));
	plog_hex("MMFR0", rsr("id_aa64mmfr0_el1"));
	plog_hex("ramoops", rz);
	plog_hex("ramoops size", found ? found_sz : 0);

	uint64_t fb = 0, fb_sz = 0;
	int have_fb = fdt_find_reserved(dtb, "framebuffer", &fb, &fb_sz);
	if (!have_fb)
		have_fb = fdt_find_reserved(dtb, "splash", &fb, &fb_sz);
	plog_hex("FB", have_fb ? fb : 0);
	plog_hex("FB size", fb_sz);
	if (have_fb && fb) {
		con_init(fb);
		con_clear(0);
		con_str("SHIM", "pixel7 (panther) loader");
		con_skip();
	} else {
		plog_str("no FB reserved-memory node; pstore only\n");
	}

	const char *why = loader_boot(dtb);
	plog_str("LOADER: ");
	plog_str(why);
	plog_str("\n");
	con_str("LOADER", why);
	con_str("RESET", "in 15s");
	delay_ms(15000);
	psci_reset();
}

void secondary_main(uint64_t idx)
{
	(void)idx;
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)regs;
	plog_hex("EXC slot", slot);
	plog_hex("ESR", rsr("esr_el1"));
	plog_hex("FAR", rsr("far_el1"));
	plog_hex("ELR", rsr("elr_el1"));
	psci_reset();
}
