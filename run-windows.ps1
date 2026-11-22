<#
.SYNOPSIS
    Run Loop-OS on Windows using QEMU (builds in WSL2 automatically)
.DESCRIPTION
    This script:
    1. Builds kernel + ISO in WSL2 (Ubuntu)
    2. Copies ISO to Windows temp
    3. Launches QEMU Windows native
.REQUIRES
    - WSL2 with Ubuntu installed
    - QEMU for Windows in PATH (winget install QEMU.QEMU)
    - Loop-OS source at D:\projetos\Agente\loop-ai\Workspace\loop-os\loop-os
#>

param(
    [switch]$RebuildToolchain,
    [switch]$NoBuild,
    [string]$QemuArgs = "-m 512M -display gtk",
    [string]$IsoPath = "D:\loop-os.iso"
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = "D:\projetos\Agente\loop-ai\Workspace\loop-os\loop-os"
$WslDistro = "Ubuntu"
$WslProjectPath = "/mnt/d/projetos/Agente/loop-ai/Workspace/loop-os/loop-os"

function Write-Info { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Cyan }
function Write-Ok   { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Green }
function Write-Err  { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Red }

# Check QEMU
if (-not (Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue)) {
    Write-Err "QEMU não encontrado no PATH. Instale: winget install QEMU.QEMU"
    exit 1
}

# Check WSL
if (-not (wsl -l -v | Select-String $WslDistro)) {
    Write-Err "WSL distro '$WslDistro' não encontrada. Instale: wsl --install -d Ubuntu"
    exit 1
}

if (-not $NoBuild) {
    Write-Info "Construindo no WSL2 ($WslDistro)..."
    
    $buildCmd = @"
set -euo pipefail
cd $WslProjectPath
export PATH="\$HOME/loop-os/toolchain/local/bin:\$PATH"
"@
    
    if ($RebuildToolchain) {
        $buildCmd += "`nmake toolchain"
    }
    
    $buildCmd += "`nmake kernel`nmake iso"
    
    Write-Info "Executando build no WSL (pode demorar alguns minutos)..."
    wsl -d $WslDistro -e bash -c $buildCmd
    
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build falhou no WSL"
        exit 1
    }
    
    Write-Ok "Build concluído no WSL"
}

# Copy ISO from WSL to Windows
Write-Info "Copiando ISO do WSL para Windows..."
$wslIsoPath = "\\wsl$\$WslDistro$WslProjectPath\build\loop-os.iso"
if (-not (Test-Path $wslIsoPath)) {
    Write-Err "ISO não encontrado em $wslIsoPath"
    exit 1
}

Copy-Item -Force $wslIsoPath $IsoPath
Write-Ok "ISO copiado para $IsoPath"

# Run QEMU
Write-Info "Iniciando QEMU..."
Write-Info "Comando: qemu-system-x86_64.exe $QemuArgs -drive file=$IsoPath,format=raw,if=virtio -serial stdio"

& qemu-system-x86_64.exe $QemuArgs -drive "file=$IsoPath,format=raw,if=virtio" -serial stdio