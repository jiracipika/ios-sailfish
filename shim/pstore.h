#pragma once
#include <stdint.h>

void plog_init(void);
/* Redirect the log to a persistent_ram console zone ( Pixel 7: gs201 ramoops). */
void plog_init_at(uintptr_t base);
void plog_append(void);
void plog_str(const char *s);
void plog_hex(const char *label, uint64_t v);
