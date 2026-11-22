param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    New-Item -ItemType Directory -Force evidence | Out-Null
    & $Python scripts/test_native.py 2>&1 | Tee-Object -FilePath evidence/native-tests.log
    if ($LASTEXITCODE -ne 0) { throw 'Native C++ tests failed' }
    & $Python -m platformio run -e sim 2>&1 | Tee-Object -FilePath evidence/build-sim.log
    if ($LASTEXITCODE -ne 0) { throw 'ESP32 NTP simulation build failed' }
    & $Python -m platformio run -e sim_mock 2>&1 | Tee-Object -FilePath evidence/build-sim-mock.log
    if ($LASTEXITCODE -ne 0) { throw 'ESP32 mock simulation build failed' }
    Write-Output 'Native tests and both firmware builds passed. Wokwi execution is a separate check.'
} finally {
    Pop-Location
}
