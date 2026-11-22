# Loop-OS

Sistema operacional simples com kernel próprio, desktop environment, aplicações nativas e ISO bootável (Live + Install).

> **⚠️ Projeto extremamente ambicioso** — Criar um SO do zero leva anos para uma equipe. Este roteiro divide em fases incrementais para que cada etapa entregue algo testável.

---

## 🎯 Visão Geral

| Componente | Tecnologia Base |
|------------|-----------------|
| **Bootloader** | Limine (moderno, UEFI + BIOS) |
| **Kernel** | x86_64, C + Assembly, monolithic modular |
| **Userspace** | Musl libc, syscalls próprias, ELF loader |
| **Graphics** | Framebuffer → DRM/KMS (futuro), compositor próprio |
| **Window Manager** | Tiling + floating hybrid, compositor Wayland-like |
| **Desktop** | Painel, ícones, menu, notificações |
| **Apps** | File manager, image viewer, text editor, browser (minimal) |
| **ISO** | Limine + XORRISO, live boot com overlayfs |

---

## 📦 Estrutura do Repositório

```
loop-os/
├── README.md              # Este arquivo
├── docs/                  # Documentação técnica
│   ├── architecture.md
│   ├── syscalls.md
│   ├── filesystem.md
│   └── graphics.md
├── toolchain/             # Scripts de build do cross-compiler
│   ├── build-toolchain.sh
│   └── toolchain.cmake
├── kernel/                # Kernel Loop-OS
│   ├── arch/x86_64/       # Código específico x86_64
│   │   ├── boot.S         # Entry point (Limine)
│   │   ├── gdt.S
│   │   ├── idt.S
│   │   ├── paging.S
│   │   └── interrupts.S
│   ├── mm/                # Memory management
│   │   ├── pmm.c/h        # Physical memory manager
│   │   ├── vmm.c/h        # Virtual memory manager
│   │   └── heap.c/h       # Kernel heap (slab/bump)
│   ├── fs/                # Filesystem drivers
│   │   ├── vfs.c/h        # Virtual filesystem switch
│   │   ├── fat32.c/h
│   │   ├── ext2.c/h
│   │   └── ramfs.c/h
│   ├── drivers/           # Drivers de hardware
│   │   ├── framebuffer.c/h
│   │   ├── keyboard.c/h   # PS/2 + USB HID
│   │   ├── mouse.c/h
│   │   ├── timer.c/h      # PIT + HPET + TSC
│   │   ├── serial.c/h
│   │   └── pci.c/h
│   ├── sched/             # Scheduler
│   │   ├── process.c/h
│   │   ├── thread.c/h
│   │   └── syscalls.c/h
│   ├── ipc/               # Inter-process communication
│   │   ├── pipe.c/h
│   │   ├── socket.c/h
│   │   └── sharedmem.c/h
│   ├── lib/               # Kernel library (string, printf, etc)
│   │   ├── string.c/h
│   │   ├── printf.c/h
│   │   └── list.c/h
│   ├── kernel.ld          # Linker script
│   ├── Makefile
│   └── limine.cfg         # Config do bootloader
├── userspace/             # Userspace (init, libc, apps)
│   ├── libc/              # Musl fork ou minimal libc
│   │   ├── include/
│   │   ├── src/
│   │   └── Makefile
│   ├── init/              # Primeiro processo (PID 1)
│   │   ├── init.c
│   │   └── rc.d/          # Scripts de inicialização
│   ├── libgui/            # Biblioteca gráfica compartilhada
│   │   ├── window.c/h
│   │   ├── widget.c/h
│   │   ├── compositor.c/h
│   │   ├── event.c/h
│   │   └── font.c/h
│   ├── wm/                # Window Manager / Compositor
│   │   ├── main.c
│   │   ├── layout.c/h
│   │   ├── decoration.c/h
│   │   └── ipc.c/h        # Protocolo WM ↔ apps
│   ├── desktop/           # Desktop Environment
│   │   ├── panel.c/h      # Painel (barra de tarefas)
│   │   ├── launcher.c/h   # Menu de aplicações
│   │   ├── icons.c/h      # Ícones na área de trabalho
│   │   ├── notifications.c/h
│   │   └── settings.c/h
│   ├── apps/              # Aplicações nativas
│   │   ├── filemanager/   # Explorador de arquivos
│   │   ├── imageviewer/   # Visualizador de imagens
│   │   ├── texteditor/    # Editor de texto
│   │   └── browser/       # Navegador (minimal, WebView-based)
│   └── Makefile
├── iso/                   # Geração de ISO
│   ├── build-iso.sh
│   ├── limine.cfg
│   ├── grub.cfg           # Fallback GRUB
│   ├── overlay/           # Overlay para live boot
│   │   ├── etc/
│   │   ├── var/
│   │   └── root/
│   └── packages/          # Lista de pacotes para install
├── tests/                 # Testes unitários e integração
│   ├── kernel/
│   └── userspace/
├── scripts/               # Scripts auxiliares
│   ├── qemu-run.sh
│   ├── qemu-debug.sh
│   ├── docker-build.sh
│   └── format-code.sh
├── .github/workflows/     # CI/CD
│   ├── build.yml
│   └── test.yml
├── Makefile               # Build principal
├── toolchain.cmake        # CMake toolchain file
└── LICENSE
```

---

## Estado atual

| Fase | Situação |
|------|----------|
| 0 - Toolchain e build | Pronta: `make toolchain`, `make`, `make iso`, `make run` |
| 1 - Kernel core | Pronta: GDT/IDT, PIC, PMM, VMM, heap (`kmalloc`/`kfree`), timer, teclado, mouse, framebuffer + console. 69 testes de memória rodam no boot |
| 2 - Drivers e FS | Parcial: initrd somente leitura (tar). Faltam PCI, VFS completo, FAT32, ext2 |
| 3 - Userspace básico | Funcionando em ring 3: SYSCALL/SYSRET, processos, ELF, scheduler preemptivo, `init` e `sh`. Sem `fork`/`execve` (há `spawn`), sem FS gravável |
| 4 a 7 | Não iniciadas |

### Como rodar

```bash
make toolchain      # uma vez (cross-compiler x86_64-elf)
export PATH="$PWD/toolchain/local/bin:$PATH"
make iso            # kernel + userspace + initrd + ISO
qemu-system-x86_64 -cdrom build/loop-os.iso -m 256 -serial stdio
```

O kernel inicia `/sbin/init`, que inicia `/bin/sh`. Programas em `/bin`:
`ls cat hello ps kill sleep spin crash`. Quando o `init` termina, o kernel cai no
seu shell interno (`help` lista os comandos).

O código do userspace está em `userspace/` (ulib + programas). O scaffold antigo,
que nunca compilou, ficou em `userspace/legacy/`. Os arquivos de `rootfs/` entram
no initrd. Sobre sistemas de arquivos: o initrd é somente leitura.

## 🚀 Fases de Desenvolvimento

### **Fase 0 — Toolchain & Build System** ✅ *Iniciar aqui*
- [ ] Cross-compiler GCC/Clang para x86_64-elf (musl)
- [ ] CMake toolchain file
- [ ] Makefile principal com targets: `kernel`, `userspace`, `iso`, `run`, `debug`
- [ ] Dockerfile para build reprodutível
- [ ] CI GitHub Actions (build + test básico)
- [ ] Scripts QEMU (run, debug com GDB)

**Entregável**: `make run` boota kernel mínimo no QEMU com serial output.

---

### **Fase 1 — Kernel Core** 🔲
- [ ] Entry point Limine (boot.S) → C kernel_main
- [ ] GDT, IDT, ISR/IRQ handlers
- [ ] Paging (4-level, higher-half kernel, recursive mapping)
- [ ] Physical Memory Manager (bitmap/frame allocator)
- [ ] Virtual Memory Manager (map/unmap, kernel heap)
- [ ] Kernel heap (bump allocator → slab)
- [ ] Timer (PIT → HPET/TSC), ticks, sleep/usleep
- [ ] Serial logging (COM1), panic handler, stack trace
- [ ] Interrupt-driven keyboard (PS/2) básico

**Entregável**: Kernel boota, mostra log no serial, gerencia memória, trata interrupções.

---

### **Fase 2 — Drivers & Filesystem** 🔲
- [ ] Framebuffer driver (Linear framebuffer via Limine/UEFI GOP)
- [ ] PCI enumeration, BAR mapping
- [ ] PS/2 mouse + USB HID (teclado/mouse)
- [ ] VFS layer (mount, open/read/write/close, opendir/readdir)
- [ ] FAT32 driver (boot partition, USB)
- [ ] ext2/ext3 driver (root partition)
- [ ] Ramfs/tmpfs para /dev, /run, /tmp
- [ ] Device nodes (/dev/fb0, /dev/kbd, /dev/mouse, /dev/null, /dev/zero)

**Entregável**: Kernel monta partitions, lê arquivos, framebuffer funcional, input funcional.

---

### **Fase 3 — Userspace Básico** 🔲
- [ ] Syscall infrastructure (sysenter/syscall, sysret/iretq)
- [ ] Process/thread creation (fork, execve, exit, wait)
- [ ] ELF loader (userspace binaries)
- [ ] Minimal libc (musl fork ou nova: syscalls, malloc, string, stdio, pthread)
- [ ] Init (PID 1): monta fs, lança getty/login, inicia WM
- [ ] Shell básico (builtins: cd, ls, cat, echo, export, exec)
- [ ] Userspace heap, mmap, brk

**Entregável**: Boota para shell interativo no framebuffer com teclado.

---

### **Fase 4 — Graphics Stack & Window Manager** 🔲
- [ ] libgui: janelas, widgets (button, label, input, scroll), layout (flex/grid)
- [ ] Font rendering (stb_truetype ou FreeType minimal)
- [ ] Compositor: double/triple buffer, damage tracking, vsync
- [ ] WM: protocolo IPC (shared memory + event ring buffer)
- [ ] Window management: tiling (master/stack), floating, workspaces
- [ ] Decorações: título, botões (close/max/min), resize handles
- [ ] Input routing: focus follows mouse, click-to-focus, keybindings

**Entregável**: WM roda, abre janelas, redimensiona, troca workspaces, compositor vsync.

---

### **Fase 5 — Desktop Environment** 🔲
- [ ] Panel: clock, system tray, workspace switcher, app menu button
- [ ] Application launcher (menu estilo start / rofi-like)
- [ ] Desktop icons: grid, drag-drop, context menu
- [ ] Notifications daemon (toast, persistência, ações)
- [ ] Settings: wallpaper, theme, keybindings, network, display
- [ ] Session management: logout, reboot, shutdown via syscalls
- [ ] Theme engine: CSS-like ou estruturado (colors, radii, fonts)

**Entregável**: Desktop usável com painel, menu, ícones, notificações, temas.

---

### **Fase 6 — Aplicações Core** 🔲
| App | Features Mínimas |
|-----|------------------|
| **File Manager** | Tree view, list/icon view, copy/move/delete/rename, mime types, bookmarks, address bar, thumbnails |
| **Image Viewer** | Zoom/pan, fit/window, next/prev, slideshow, EXIF, formatos: PNG, JPEG, BMP, GIF, WebP |
| **Text Editor** | Syntax highlight, line numbers, tabs, search/replace, undo/redo, encoding, large files |

- [ ] Cada app como binário ELF independente
- [ ] Integração com WM (ícones, mime, atalhos)
- [ ] Thumbnailer background service

**Entregável**: 3 apps funcionais, integrados ao desktop.

---

### **Fase 7 — Navegador Web (Mínimo)** 🔲
> **Realidade**: Engine de browser do zero = anos. Opções pragmáticas:
> - **Portar NetSurf** (leve, C, framebuffer backend)
> - **Usar WebKitGTK/WPE** via shared library (pesado)
> - **Minimal HTML/CSS renderer** próprio (muito limitado)

**Recomendação**: Integrar **NetSurf** com framebuffer backend próprio, ou **WPE WebKit** se RAM > 1GB.

- [ ] Backend framebuffer para NetSurf / WPE
- [ ] URL bar, tabs, bookmarks, history
- [ ] Download manager
- [ ] Cookie/storage persistence

**Entregável**: Navega sites básicos (HTML/CSS, JS limitado).

---

### **Fase 8 — ISO Live + Installer** 🔲
- [ ] Limine config para UEFI + BIOS (dual boot)
- [ ] Initramfs com busybox-like tools + loop-os init
- [ ] Overlayfs para live boot (read-only root + rw overlay)
- [ ] Live user auto-login, desktop carrega direto
- [ ] Installer (CLI ou TUI): partition, format, copy rootfs, install bootloader
- [ ] Persistência opcional (arquivo/partição overlay)
- [ ] ISO híbrida (dd para USB + boot UEFI/BIOS)
- [ ] Assinatura SB (opcional, futuro)

**Entregável**: `loop-os.iso` boota em QEMU/hardware real, modo live e instala.

---

### **Fase 9 — Polish, Tests, Docs** 🔲
- [ ] Testes unitários kernel (mm, sched, fs, syscalls)
- [ ] Testes integração userspace
- [ ] Fuzzing syscalls (trinity-like)
- [ ] Documentação: arquitetura, syscalls, driver model, contribuição
- [ ] Performance profiling (boot time, memory, latency)
- [ ] Bug bash, hardening (stack canaries, KASLR, KPTI, SECCOMP-like)
- [ ] Acessibilidade (screen reader hooks, high contrast, keyboard nav)
- [ ] Localização (i18n, pt-BR default)

---

## 🛠 Como Buildar (após Fase 0)

```bash
# Pré-requisitos: Docker, ou toolchain local (x86_64-elf-gcc, limine, xorriso, qemu)

# Build tudo
make

# Só kernel
make kernel

# Só userspace
make userspace

# Gera ISO
make iso

# Roda no QEMU (KVM se disponível)
make run

# Debug com GDB
make debug
```

---

## 📚 Referências Técnicas

| Tópico | Links |
|--------|-------|
| **Limine Bootloader** | https://limine-bootloader.org/ |
| **OSDev Wiki** | https://wiki.osdev.org/ |
| **Writing an OS in Rust (filosofia)** | https://os.phil-opp.com/ |
| **Musl Libc** | https://musl.libc.org/ |
| **NetSurf Browser** | https://www.netsurf-browser.org/ |
| **WPE WebKit** | https://wpewebkit.org/ |
| **stb_truetype** | https://github.com/nothings/stb |
| **Overlayfs** | https://docs.kernel.org/filesystems/overlayfs.html |

---

## 🎨 Design Guidelines (para skills)

Quando usar skills de design/animação:
- **animate / animate-expo** — transições de janelas, menus, tooltips, feedback de botões
- **apple-design** — gestos, física de molas, materiais translúcidos, tipografia SF-like
- **emil-design-eng** — polimento invisível: micro-interações, estados vazios, loading skeletons
- **mobile-native** — se portar para touch no futuro
- **review-animations / improve-animations** — auditar motion no WM/compositor
- **break-ui** — testar apps com dados extremos (nomes longos, 10k arquivos, Unicode, RTL)

---

## ⚖️ Licença

MIT — Veja [LICENSE](LICENSE)

---

## 🤝 Contribuindo

Este é um projeto educacional/experimental. Issues e PRs bem-vindos para fases específicas.

> **Dica**: Comece pela **Fase 0**. Cada fase deve compilar e rodar isoladamente antes de avançar.