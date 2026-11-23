@echo off
REM Roda o Loop-OS no QEMU da pasta emulador, com placa de rede Intel e1000.
REM Dentro do sistema o seu PC aparece como 10.0.2.2.
REM Para testar download: em outro terminal rode "python -m http.server 8000" numa pasta com arquivos.

cd /d "%~dp0emulador\QemuSimpleBoot\qemu"
set SDL_VIDEODRIVER=windib
set QEMU_AUDIO_DRV=none

qemu-system-x86_64.exe -L . -m 256 -cdrom "%~dp0build\loop-os.iso" -boot d -vga std -M pc -localtime -net nic,model=e1000 -net user
