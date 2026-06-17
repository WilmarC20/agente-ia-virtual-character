# Descarga modelo RVC Luis Miguel (TexX) desde Hugging Face:
# https://huggingface.co/TexX/LuisMiguel_RVC
#
# Variantes:
#   20A   — Luis Miguel [20 Años / 1990] 32K, Rmvpe (demo en Discord AI HUB)  [default]
#   1999  — otra época, zip mas grande

param(
    [ValidateSet("20A", "1999")]
    [string]$Variant = "20A"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$zipName = if ($Variant -eq "20A") { "LuisMiguel20A.zip" } else { "LuisMiguel_1999.zip" }
$outBase = if ($Variant -eq "20A") { "LuisMiguel20A" } else { "LuisMiguel1999" }

$py = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
if (-not (Test-Path $py)) { $py = "python" }

& $py -m pip install -q huggingface_hub

$dest = Join-Path $PSScriptRoot "rvc_models"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Descargando $zipName (variante Discord: 20A = album 20 Anos, 32K)..." -ForegroundColor Cyan
& $py -c @"
from huggingface_hub import hf_hub_download
from pathlib import Path
import zipfile
import shutil

dest = Path(r'$dest')
zip_path = Path(hf_hub_download(
    repo_id='TexX/LuisMiguel_RVC',
    filename='$zipName',
    local_dir=dest / 'hf_cache',
))
extract = dest / 'luismiguel_extracted'
if extract.exists():
    shutil.rmtree(extract)
extract.mkdir(parents=True)
print('Extracting', zip_path)
with zipfile.ZipFile(zip_path, 'r') as z:
    z.extractall(extract)

pths = list(extract.rglob('*.pth'))
indexes = list(extract.rglob('*.index'))
if not pths:
    raise SystemExit('No .pth found inside zip')
pth = pths[0]
idx = indexes[0] if indexes else None

out_pth = dest / '$outBase.pth'
shutil.copy2(pth, out_pth)
print('Model:', out_pth)
if idx:
    out_idx = dest / '$outBase.index'
    shutil.copy2(idx, out_idx)
    print('Index:', out_idx)
else:
    print('WARN: no .index in zip — RVC funcionara pero con menos fidelidad')

print('')
print('Add to secrets.local.ps1:')
print(r'  `$env:RVC_MODEL_PATH = \"' + str(out_pth).replace('\\','\\\\') + '\"')
if idx:
    print(r'  `$env:RVC_INDEX_PATH = \"' + str(out_idx).replace('\\','\\\\') + '\"')
print(r'  `$env:RVC_F0_METHOD = \"rmvpe\"')
print(r'  `$env:RVC_RESAMPLE_SR = \"0\"')
"@

if ($LASTEXITCODE -ne 0) { exit 1 }
Write-Host ""
Write-Host "Listo. Los WAV de demo en Discord (No Podras, etc.) son COVERS con guia vocal real," -ForegroundColor Yellow
Write-Host "no Bark — sube una guia WAV en /sing-test para el mismo flujo." -ForegroundColor Yellow
Write-Host "Reinicia .\start.ps1" -ForegroundColor Green
