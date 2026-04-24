#pragma once

#include <stdint.h>

namespace twang_storage {

// Reads the Settings struct from NVS into g_settings. If the schema version
// doesn't match or the blob is missing, writes defaults and returns false.
bool load();

// Writes g_settings to NVS. Idempotent.
bool save();

} // namespace twang_storage
