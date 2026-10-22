/*
 * Just enough flattened-device-tree parsing to find what ABL put in
 * /chosen (initrd location). Big-endian, 4-byte aligned tokens.
 */
#include "fdt.h"

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static uintptr_t align4(uintptr_t a)
{
	return (a + 3) & ~3UL;
}

static uintptr_t skip_str(uintptr_t p)
{
	/* byte loads are fine on Device memory as long as they are aligned to 1 */
	while (*(volatile char *)p)
		p++;
	return p + 1;
}

int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);		/* off_dt_struct */
	uintptr_t strings = fdt + be32(fdt + 12);	/* off_dt_strings */
	int depth = 0, in_chosen = 0;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			in_chosen = depth == 2 && streq(name, "chosen");
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_chosen)
				return 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t val = p + 8;
			if (in_chosen && streq((const char *)(strings + nameoff), prop)) {
				if (len == 4)
					*out = be32(val);
				else if (len == 8)
					*out = (uint64_t)be32(val) << 32 | be32(val + 4);
				else
					return 0;
				return 1;
			}
			p = align4(val + len);
			break;
		}
		case FDT_NOP:
			break;
		default:	/* FDT_END or garbage */
			return 0;
		}
	}
}

/* "ramoops@fd3ff000" matches prefix "ramoops" */
static int prefix_match(const char *name, const char *prefix)
{
	while (*prefix)
		if (*name++ != *prefix++)
			return 0;
	return 1;
}

static uint64_t cells_val(uintptr_t val, int cells)
{
	uint64_t v = 0;
	for (int i = 0; i < cells && i < 2; i++)
		v = v << 32 | be32(val + 4 * i);
	return v;
}

/*
 * Walk into /reserved-memory (its #address-cells/#size-cells qualify every
 * child's reg; gs201 uses 2/1), then collect the first reg word-pair of the
 * matching child. want_prop: instead of reg, read that property of the child.
 */
static int reserved_scan(uintptr_t fdt, const char *prefix, const char *want_prop,
			 uint64_t *pa, uint64_t *size, uint64_t *val)
{
	if (!fdt || be32(fdt) != FDT_MAGIC)
		return 0;
	uintptr_t p = fdt + be32(fdt + 8);
	uintptr_t strings = fdt + be32(fdt + 12);
	int depth = 0, in_rm = 0, in_node = 0, ac = 2, sc = 2;

	for (;;) {
		uint32_t tok = be32(p);
		p += 4;
		switch (tok) {
		case FDT_BEGIN_NODE: {
			const char *name = (const char *)p;
			depth++;
			if (depth == 2) {
				in_rm = streq(name, "reserved-memory");
				ac = sc = 2;
			} else if (in_rm && depth == 3) {
				in_node = prefix_match(name, prefix);
			}
			p = align4(skip_str(p));
			break;
		}
		case FDT_END_NODE:
			if (in_node)
				return 0;	/* matched node ended without the property */
			if (depth == 2)
				in_rm = 0;
			depth--;
			break;
		case FDT_PROP: {
			uint32_t len = be32(p), nameoff = be32(p + 4);
			uintptr_t v = p + 8;
			const char *pn = (const char *)(strings + nameoff);
			if (in_rm && depth == 2) {
				if (streq(pn, "#address-cells") && len == 4)
					ac = be32(v);
				else if (streq(pn, "#size-cells") && len == 4)
					sc = be32(v);
			} else if (in_node && depth == 3) {
				if (want_prop ? streq(pn, want_prop) : streq(pn, "reg")) {
					if (want_prop) {
						if (len == 4)
							*val = be32(v);
						else if (len == 8)
							*val = (uint64_t)be32(v) << 32 | be32(v + 4);
						else
							return 0;
					} else {
						if (len < (uint32_t)((ac + sc) * 4))
							return 0;
						*pa = cells_val(v, ac);
						*size = cells_val(v + ac * 4, sc);
					}
					return 1;
				}
			}
			p = align4(v + len);
			break;
		}
		case FDT_NOP:
			break;
		default:
			return 0;
		}
	}
}

int fdt_find_reserved(uintptr_t fdt, const char *prefix, uint64_t *pa, uint64_t *size)
{
	uint64_t v;
	return reserved_scan(fdt, prefix, 0, pa, size, &v);
}

int fdt_reserved_u64(uintptr_t fdt, const char *prefix, const char *prop, uint64_t *out)
{
	uint64_t pa, size;
	return reserved_scan(fdt, prefix, prop, &pa, &size, out);
}
