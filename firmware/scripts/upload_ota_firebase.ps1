# Upload PlatformIO firmware.bin files to Firebase Storage for LavaCore OTA.
#
# Prereqs:
#   - Google Cloud SDK (gsutil) installed
#   - `gcloud auth login` (or ADC) with permission to write the bucket
#   - Builds present: run from repo root:
#       cd firmware && pio run -e esp32dev -e esp32-gpio2 -e esp32-gpio5
#
# Usage (PowerShell):
#   cd firmware\scripts
#   .\upload_ota_firebase.ps1
#
# Default bucket matches `storageBucket` in `lib/firebase_options_dev.dart`
# (project led-runner).

$ErrorActionPreference = "Stop"
$firmwareRoot = Split-Path -Parent $PSScriptRoot
$bucket = "gs://led-runner.firebasestorage.app"

$uploads = @(
    @{ PioEnv = "esp32dev";    Dest = "firmware/ota/esp32dev.bin" },
    @{ PioEnv = "esp32-gpio2"; Dest = "firmware/ota/esp32-gpio2.bin" },
    @{ PioEnv = "esp32-gpio5"; Dest = "firmware/ota/esp32-gpio5.bin" }
)

foreach ($u in $uploads) {
    $src = Join-Path $firmwareRoot ".pio\build\$($u.PioEnv)\firmware.bin"
    if (-not (Test-Path $src)) {
        Write-Error "Missing $src - build first: pio run -e $($u.PioEnv)"
    }
    $uri = "$bucket/$($u.Dest)"
    Write-Host "Uploading $src -> $uri"
    gsutil -h "Content-Type:application/octet-stream" cp $src $uri
}

Write-Host 'Done. Public read access is configured in firebase/storage.rules under firmware/ota/.'
