# Builds the release zip:  InfinityClicker-v<version>-win64.zip (+ .sha256) in .\dist
#   ./tools/package.ps1 -Version 1.0.0 -Exe InfinityClicker.exe
param(
    [string]$Version = "1.0.0",
    [string]$Exe = "InfinityClicker.exe",
    [string]$OutDir = "dist"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Test-Path $Exe)) { throw "$Exe not found - run build_release.bat first" }

$name = "InfinityClicker-v$Version-win64"
$stage = Join-Path $OutDir $name
Remove-Item -Recurse -Force $OutDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $Exe (Join-Path $stage "InfinityClicker.exe")
Copy-Item "LICENSE" (Join-Path $stage "LICENSE.txt")
Copy-Item "tools/dist/README.txt" (Join-Path $stage "README.txt")

# Dear ImGui is compiled into the exe; its licence has to travel with the binary.
$imgui = Get-Content "third_party/imgui/LICENSE.txt" -Raw
"Third-party software included in InfinityClicker.exe`r`n`r`nDear ImGui (https://github.com/ocornut/imgui)`r`n`r`n$imgui" |
    Set-Content (Join-Path $stage "THIRD_PARTY_LICENSES.txt") -Encoding UTF8

$zip = Join-Path $OutDir "$name.zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content (Join-Path $OutDir "$name.zip.sha256") -Encoding ASCII
Remove-Item -Recurse -Force $stage
"$zip  ($([math]::Round((Get-Item $zip).Length / 1KB)) KB)"
"sha256 $hash"
