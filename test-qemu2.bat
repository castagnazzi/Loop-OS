@echo off
cd /d D:\projetos\Agente\loop-ai\Workspace\loop-os\loop-os\emulador\QemuSimpleBoot\qemu
qemu-system-x86_64.exe -version
qemu-system-x86_64.exe -m 512M -nographic -serial stdio -display none -no-reboot -boot d