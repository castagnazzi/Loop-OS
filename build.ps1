<#
.SYNOPSIS
    Loop-OS Build Script for Windows (PowerShell)
.DESCRIPTION
    Wrapper around Docker to build and run Loop-OS on Windows.
    Requires: Docker Desktop with WSL2 backend.
.EXAMPLE
    .\build.ps1 toolchain   # Build cross-compiler (~10-15 min)
    .\build.ps1 build       # Compile kernel + ISO
    .\build.ps1 run         # Run in QEMU (headless, serial output)
    .\build.ps1 run-gui     # Run with GUI (needs X11/WSLg)
    .\build.ps1 debug       # Run with GDB stub on port 1234
    .\build.ps1 shell       # Interactive shell in build container
    .\build.ps1 clean       # Remove build artifacts
    .\build.ps1 all         # toolchain + build + run
#>

param(
    [ValidateSet('toolchain','build','run','run-gui','debug','shell','clean','all','help')]
    [string]$Command = 'help'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot

function Write-Info { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Cyan }
function Write-Ok   { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Green }
function Write-Err  { param($msg) Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $msg" -ForegroundColor Red }

function Check-Docker {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Write-Err "Docker não encontrado. Instale Docker Desktop com WSL2 backend."
        exit 1
    }
    try { docker version --format '{{.Server.Version}}' | Out-Null }
    catch { Write-Err "Docker daemon não está rodando. Inicie o Docker Desktop."; exit 1 }
}

function Docker-BuildImage {
    Write-Info "Construindo imagem Docker..."
    docker build -t loop-os-builder -f "$ProjectRoot\toolchain\Dockerfile" "$ProjectRoot"
    Write-Ok "Imagem pronta: loop-os-builder"
}

function Docker-Run {
    param([string[]]$Cmd, [switch]$Interactive, [switch]$Gui)
    $volumes = "-v", "$(cygpath -w $ProjectRoot):/src"
    $workdir = "-w", "/src"
    $image = "loop-os-builder"
    $entrypoint = if ($Interactive) { "-it", "--entrypoint", "/bin/bash" } else { "" }
    $ports = if ($Command -eq 'debug') { "-p", "1234:1234" } else { "" }
    $guiArgs = if ($Gui) {
        "-v", "/tmp/.X11-unix:/tmp/.X11-unix",
        "-v", "$env:USERPROFILE\.Xauthority:/root/.Xauthority",
        "-e", "DISPLAY=$env:DISPLAY",
        "-e", "QT_X11_NO_MITSHM=1"
    } else { "" }
    
    docker run --rm $volumes $workdir $ports $guiArgs $entrypoint $image $Cmd
}

function Run-InContainer {
    param([string]$MakeTarget)
    Docker-Run -Cmd @("make", $MakeTarget)
}

switch ($Command) {
    'toolchain' {
        Check-Docker
        Docker-BuildImage
        Write-Info "Compilando toolchain (pode demorar 10-20 min)..."
        Docker-Run -Cmd @("make", "toolchain")
        Write-Ok "Toolchain pronta em toolchain/local/bin/"
    }
    'build' {
        Check-Docker
        if (-not (Test-Path "$ProjectRoot\toolchain\local\bin\$($Env:TARGET)-gcc")) {
            Write-Info "Toolchain não encontrada. Buildando..."
            & $PSCommandPath toolchain
        }
        Write-Info "Compilando kernel + ISO..."
        Run-InContainer "kernel"
        Run-InContainer "iso"
        Write-Ok "ISO gerada: build/loop-os.iso"
    }
    'run' {
        Check-Docker
        Write-Info "Iniciando QEMU (headless, serial no console)..."
        Write-Info "Pressione Ctrl+A X para sair"
        Docker-Run -Cmd @("qemu-system-x86_64", "-m", "512M", "-nographic", "-serial", "stdio", "-drive", "file=build/loop-os.iso,format=raw,if=virtio")
    }
    'run-gui' {
        Check-Docker
        Write-Info "Iniciando QEMU com GUI (precisa WSLg ou X Server)..."
        Docker-Run -Gui -Cmd @("make", "run")
    }
    'debug' {
        Check-Docker
        Write-Info "Iniciando QEMU com GDB stub na porta 1234..."
        Write-Info "Conecte com: gdb -ex 'target remote localhost:1234' build/kernel.elf"
        Docker-Run -Cmd @("qemu-system-x86_64", "-m", "512M", "-nographic", "-serial", "stdio", "-drive", "file=build/loop-os.iso,format=raw,if=virtio", "-s", "-S")
    }
    'shell' {
        Check-Docker
        Write-Info "Entrando no container de build..."
        Docker-Run -Interactive -Cmd @()
    }
    'clean' {
        Write-Info "Limpando artefatos de build..."
        if (Test-Path "$ProjectRoot\build") { Remove-Item -Recurse -Force "$ProjectRoot\build" }
        if (Test-Path "$ProjectRoot\toolchain\build-toolchain") { Remove-Item -Recurse -Force "$ProjectRoot\toolchain\build-toolchain" }
        Write-Ok "Limpeza concluída"
    }
    'all' {
        & $PSCommandPath toolchain
        & $PSCommandPath build
        & $PSCommandPath run
    }
    default {
        Write-Host @"
Loop-OS Build Script (PowerShell + Docker)

Uso: .\build.ps1 <comando>

Comandos:
  toolchain   - Build cross-compiler GCC/musl (~10-20 min)
  build       - Compila kernel + gera ISO
  run         - Roda no QEMU (headless, serial output)
  run-gui     - Roda com GUI (precisa WSLg/X11)
  debug       - Roda com GDB stub na porta 1234
  shell       - Shell interativo no container
  clean       - Remove build/
  all         - toolchain + build + run
  help        - Esta ajuda

Pré-requisitos:
  - Docker Desktop com WSL2 backend
  - Para GUI: WSLg (Windows 11) ou X Server (VcXsrv, etc)

Exemplo completo:
  .\build.ps1 all
"@
    }
}