@echo off
cd /d D:\projetos\Agente\loop-ai\Workspace\loop-os\loop-os\emulador\QemuSimpleBoot\qemu
echo Testing QEMU version...
qemu-system-x86_64.exe -version
pause

echo.
echo Testing QEMU boot (no disk, BIOS mode)...
qemu-system-x86_64.exe -m 512M -nographic -serial stdio -display none
pause