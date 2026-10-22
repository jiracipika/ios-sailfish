#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);
/*
 * Find a child of /reserved-memory named "prefix" or "prefix@..." (ABL adds
 * the splash framebuffer this way) and return its reg. Returns 0 if absent.
 */
int fdt_find_reserved(uintptr_t fdt, const char *prefix, uint64_t *pa, uint64_t *size);
/* Read a u32/u64 property of that same node (e.g. ramoops "console-size"). */
int fdt_reserved_u64(uintptr_t fdt, const char *prefix, const char *prop, uint64_t *out);
