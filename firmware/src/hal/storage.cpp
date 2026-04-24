#include "hal/storage.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config/settings.h"

static const char* kNvsNamespace = "twang";
static const char* kKeyBlob      = "settings_v3";

Settings g_settings{};
long     g_last_input_time_ms = 0;

namespace twang_storage {

bool load() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
        Serial.println("[storage] NVS open failed, resetting defaults");
        twang_settings::reset_defaults();
        save();
        return false;
    }

    const size_t len = prefs.getBytesLength(kKeyBlob);
    if (len != sizeof(Settings)) {
        Serial.printf("[storage] size mismatch (found %u, want %u), resetting\n",
                      (unsigned)len, (unsigned)sizeof(Settings));
        prefs.end();
        twang_settings::reset_defaults();
        save();
        return false;
    }

    Settings tmp{};
    prefs.getBytes(kKeyBlob, &tmp, sizeof(tmp));
    prefs.end();

    if (tmp.schema_version != SETTINGS_SCHEMA_VERSION) {
        Serial.printf("[storage] schema %u != %u, resetting\n",
                      tmp.schema_version, SETTINGS_SCHEMA_VERSION);
        twang_settings::reset_defaults();
        save();
        return false;
    }

    g_settings = tmp;
    Serial.println("[storage] loaded settings");
    return true;
}

bool save() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        Serial.println("[storage] NVS open (rw) failed");
        return false;
    }
    const size_t written = prefs.putBytes(kKeyBlob, &g_settings, sizeof(g_settings));
    prefs.end();
    return written == sizeof(g_settings);
}

} // namespace twang_storage
