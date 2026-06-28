# Instala dependencias del sketch agente-ia en la carpeta global de Arduino IDE.
# Uso: .\install-arduino-libs.ps1

$ErrorActionPreference = "Stop"
$libDir = Join-Path $env:USERPROFILE "Documents\Arduino\libraries"
New-Item -ItemType Directory -Force -Path $libDir | Out-Null

function Install-GitLib {
    param(
        [string]$Name,
        [string]$Url,
        [string]$Tag = ""
    )
    $dest = Join-Path $libDir $Name
    if (Test-Path $dest) {
        Write-Host "[OK] $Name ya existe en $dest"
        return
    }
    Write-Host "Clonando $Name..."
    if ($Tag) {
        git clone --depth 1 --branch $Tag $Url $dest
    } else {
        git clone --depth 1 $Url $dest
    }
    Write-Host "[OK] $Name instalado"
}

# JsonDocument sin tamaño = API ArduinoJson 7.x
Install-GitLib -Name "ArduinoJson" -Url "https://github.com/bblanchon/ArduinoJson.git" -Tag "v7.4.2"
Install-GitLib -Name "LovyanGFX" -Url "https://github.com/lovyan03/LovyanGFX.git"

Write-Host ""
Write-Host "Listo. Reinicia Arduino IDE y compilá firmware/agente-ia/agente-ia.ino"
