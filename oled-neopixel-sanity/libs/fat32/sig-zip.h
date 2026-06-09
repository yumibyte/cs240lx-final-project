#pragma once
#include "rpi.h"

// deflate-compress raw bytes into a single-entry ZIP archive in newly kmalloc'd memory
int sig_zip_pack(const char *entry_name, const uint8_t *raw, uint32_t raw_len,
                 uint8_t **out_zip, uint32_t *out_len);
