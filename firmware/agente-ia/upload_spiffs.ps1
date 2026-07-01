# Upload firmware/agente-ia/data/*.wav to ESP32 SPIFFS partition "spiffs"
# Partition ESP SR 16M: spiffs @ 0x610000 size 0x700000 (7 MB)

$ErrorActionPreference = "Stop"

$here = $PSScriptRoot
$dataDir = Join-Path $here "data"
$outBin = Join-Path $here "spiffs.bin"

$mkspiffs = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe"
$esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"

if (-not (Test-Path $mkspiffs)) { throw "mkspiffs not found: $mkspiffs" }
if (-not (Test-Path $esptool)) { throw "esptool not found: $esptool" }
if (-not (Test-Path $dataDir)) { throw "data folder missing: $dataDir" }

$fileCount = (Get-ChildItem $dataDir -File).Count
if ($fileCount -eq 0) { throw "No files in $dataDir" }

$port = if ($env:ESP_PORT) { $env:ESP_PORT } else { "COM18" }
$spiffsOffset = "0x610000"
$spiffsSize = 0x700000

Write-Host "Building SPIFFS image from $dataDir ($fileCount files)..."
Get-ChildItem $dataDir -File | ForEach-Object { Write-Host "  $($_.Name) ($($_.Length) bytes)" }

& $mkspiffs -c $dataDir -b 4096 -p 256 -s $spiffsSize $outBin
if (-not (Test-Path $outBin)) { throw "mkspiffs did not create $outBin" }

Write-Host "SPIFFS image: $outBin ($((Get-Item $outBin).Length) bytes)"
Write-Host "Flashing to $port at $spiffsOffset ..."

& $esptool --chip esp32s3 --port $port --baud 921600 write_flash $spiffsOffset $outBin

Write-Host "SPIFFS upload OK. Reset the board (or press EN)."
