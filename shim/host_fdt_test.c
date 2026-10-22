/*
 * Host test for the shim's FDT walkers: builds a small gs201-shaped tree
 * (#address-cells 2, #size-cells 1, ramoops@... with console-size, ABL-style
 * framebuffer node, /chosen initrd) and checks what fdt.c finds.
 * Build+run:  cd shim && cc -Wall -o out/host_fdt_test host_fdt_test.c fdt.c && out/host_fdt_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fdt.h"

static unsigned char blob[4096];
static size_t pos;
static char strblob[512];
static size_t strbytes;

static void put32(uint32_t v)
{
	blob[pos++] = v >> 24; blob[pos++] = v >> 16;
	blob[pos++] = v >> 8;  blob[pos++] = v;
}

static void putstr(const char *s)
{
	size_t n = strlen(s);
	memcpy(blob + pos, s, n);
	pos += (n + 4) & ~(size_t)3;		/* struct blocks are 4-aligned */
}

static uint32_t strtab(const char *s)
{
	size_t off = 0;
	while (off < strbytes) {
		if (!strcmp(strblob + off, s))
			return (uint32_t)off;
		off += strlen(strblob + off) + 1;
	}
	off = strbytes;
	size_t n = strlen(s) + 1;
	memcpy(strblob + off, s, n);
	strbytes += n;
	return (uint32_t)off;
}

static void prop(const char *name, const uint32_t *w, int nwords)
{
	put32(3);				/* FDT_PROP */
	put32(nwords * 4);
	put32(strtab(name));
	for (int i = 0; i < nwords; i++)
		put32(w[i]);
}

static void begin(const char *name) { put32(1); putstr(name); }
static void end(void) { put32(2); }

int main(void)
{
	static const uint32_t ac2[] = { 2 }, sc1[] = { 1 };
	static const uint32_t gsa_reg[] = { 0, 0x90200000, 0x400000 };
	static const uint32_t rz_reg[] = { 0, 0xfd3ff000, 0x400000 };
	static const uint32_t rz_cs[] = { 0x200000 };
	static const uint32_t fb_reg[] = { 0, 0xf1000000, 0x1100000 };
	static const uint32_t initrd[] = { 0, 0x8c000000, 0, 0x8f000000 };
	static const uint32_t compat[] = { 0x6d61726f, 0x73700000 };	/* "ramoops\0" */

	put32(0xd00dfeed); put32(0); put32(0); put32(0);	/* magic, total, struct, strings */
	put32(0); put32(17); put32(17); put32(0);		/* memrsv, ver, lastver, cpuid */
	put32(0); put32(0);					/* size_dt_strings, size_dt_struct */

	begin("");						/* / */
	begin("reserved-memory");
	prop("#address-cells", ac2, 1);
	prop("#size-cells", sc1, 1);
	begin("gsa@90200000");
	prop("reg", gsa_reg, 3);
	prop("no-map", NULL, 0);
	end();
	begin("ramoops@fd3ff000");
	prop("reg", rz_reg, 3);
	prop("compatible", compat, 2);
	prop("console-size", rz_cs, 1);
	end();
	begin("framebuffer");
	prop("reg", fb_reg, 3);
	prop("no-map", NULL, 0);
	end();
	end();							/* /reserved-memory */
	begin("chosen");
	prop("linux,initrd-start", initrd, 2);
	prop("linux,initrd-end", initrd + 2, 2);
	end();
	end();							/* / */
	put32(9);						/* FDT_END */

	/* strings block after the struct; patch header (struct at 40, strings, total) */
	while (pos & 3)
		pos++;
	uint32_t str_off = (uint32_t)pos;
	memcpy(blob + pos, strblob, strbytes);
	pos += strbytes;
	*(uint32_t *)(blob + 4) = __builtin_bswap32((uint32_t)pos);
	*(uint32_t *)(blob + 8) = __builtin_bswap32(40);
	*(uint32_t *)(blob + 12) = __builtin_bswap32(str_off);
	uintptr_t fdt = (uintptr_t)blob;
	{ FILE *d = fopen("/tmp/fdt.bin", "wb"); fwrite(blob, 1, pos, d); fclose(d); }

	int fails = 0;
#define CHECK(cond, msg) do { \
	if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
	else printf("ok: %s\n", msg); \
} while (0)

	uint64_t v;
	CHECK(fdt_chosen_u64(fdt, "linux,initrd-start", &v) && v == 0x8c000000UL, "initrd-start");
	CHECK(fdt_chosen_u64(fdt, "linux,initrd-end", &v) && v == 0x8f000000UL, "initrd-end");
	CHECK(!fdt_chosen_u64(fdt, "linux,initrd-missing", &v), "absent chosen prop");

	uint64_t pa, size;
	CHECK(fdt_find_reserved(fdt, "ramoops", &pa, &size), "ramoops found");
	CHECK(pa == 0xfd3ff000UL && size == 0x400000UL, "ramoops reg (cells 2/1)");
	CHECK(fdt_reserved_u64(fdt, "ramoops", "console-size", &v) && v == 0x200000, "ramoops console-size");
	CHECK(!fdt_reserved_u64(fdt, "ramoops", "pmsg-size", &v), "absent ramoops prop");
	CHECK(fdt_find_reserved(fdt, "framebuffer", &pa, &size), "framebuffer found");
	CHECK(pa == 0xf1000000UL && size == 0x1100000UL, "framebuffer reg");
	CHECK(!fdt_find_reserved(fdt, "splash", &pa, &size), "unrelated prefix must not match");
	CHECK(!fdt_find_reserved(fdt, "nonexistent", &pa, &size), "absent node");
	CHECK(fdt_find_reserved(0, "ramoops", &pa, &size) == 0, "null fdt safe");

	if (fails) {
		printf("%d FAILURES\n", fails);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
