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
| 2 - Drivers e FS | Parcial: ramfs gravavel (arquivos, pastas, `mkdir`/`rm`/`cp`), PCI, discos ATA e SATA (AHCI), particoes MBR/GPT e leitura de FAT32, NTFS e ext2/3/4 montados em `/mnt/<disco>`. Faltam NVMe, USB e escrita nos discos |
| 3 - Userspace básico | Funcionando em ring 3: SYSCALL/SYSRET, processos, `fork`/`execve`/`spawn`, pipes, ELF, scheduler preemptivo, `init` e `sh` (com `|`, `<`, `>`, `>>`, `&`) |
| 4 - Desktop | Funcionando: compositor no kernel com sombras nas janelas, icones na area de trabalho e no menu, barra de tarefas com relogio (RTC) e icone de rede, menu Iniciar (`/etc/menu`), libgui e apps: terminal, bloco de notas (com abas), arquivos, calculadora, gerenciador de tarefas, visualizador de imagens (PNG/BMP), navegador HTML simples (HTTP e arquivos locais, sem JavaScript/CSS/imagens), status da rede, jogos nativos (Cobrinha com ranking e Jogo da Velha 3D renderizado por software, com maquina facil/dificil) |
| Rede | Funcionando: PCI, placas Intel e1000, Realtek RTL8139, virtio-net e Realtek RTL8168/8169 (esta ultima ainda sem teste em hardware real), DHCP, ARP, ICMP, UDP, DNS e TCP (cliente). Apps `ping`, `wget` (HTTP), `ifconfig`, `nslookup`, `rede`. Faltam TLS/HTTPS, SMB, Wi-Fi |
| Teclado | Layout ABNT2 (padrao) com teclas mortas (acento + letra, ç) e AltGr; `teclado us` troca para o layout americano; setas e PgUp/PgDn chegam aos programas |
| 5 a 7 | Não iniciadas (audio, USB, TLS/HTTPS, JavaScript, apps maiores) |

### Como rodar

```bash
make toolchain      # uma vez (cross-compiler x86_64-elf)
export PATH="$PWD/toolchain/local/bin:$PATH"
make iso            # kernel + userspace + initrd + ISO
qemu-system-x86_64 -cdrom build/loop-os.iso -m 256 -serial stdio
```

Com framebuffer, o kernel abre o desktop e lanca o Terminal (que roda `/bin/sh`).
Programas em `/bin`: `ls cat cp mkdir rm wc hello ps kill sleep spin crash` e os
apps graficos `term notepad files calc taskmgr`. Os itens do menu Iniciar ficam em
`rootfs/etc/menu` (`Rotulo|comando`). Para testar a rede no QEMU: `-netdev user,id=n0 -device e1000,netdev=n0` (o host aparece como 10.0.2.2); tambem funcionam `-device rtl8139,netdev=n0` e `-device virtio-net-pci,netdev=n0`. No Windows, `testar-rede.bat` roda o QEMU da pasta `emulador`. Paginas HTML de exemplo em `/usr/www`; para ver um servidor do seu PC, rode `python -m http.server 8000` e abra `http://10.0.2.2:8000/` no Navegador. Imagens de exemplo em `/usr/imagens`. O app Arquivos tem arvore de pastas e lista com tamanhos. O menu tem "Sobre o Loop-OS" (assinatura e dados do sistema), Reiniciar e Desligar. O desktop abre vazio, sem terminal. O papel de parede vem de `/etc/wallpaper.lwp` (gere outro com `python tools/gen_wallpaper.py imagem.png 300`; a imagem original do mascote esta em `assets/mascote.png`). Os arquivos que vem com o sistema (`/bin`, `/etc`...) sao protegidos contra apagar e sobrescrever; `/tmp` e arquivos novos sao livres. O relogio mostra a hora do RTC: use
`-rtc base=localtime` no QEMU para a hora local.

O código do userspace está em `userspace/` (ulib + programas). O scaffold antigo,
que nunca compilou, ficou em `userspace/legacy/`. Os arquivos de `rootfs/` entram
no initrd. Sobre sistemas de arquivos: tudo vive na RAM e se perde ao reiniciar.

## 🚀 Fases de Desenvolvimento

### **Fase 0 — Toolchain & Build System** ✅
- [x] Cross-compiler GCC/Clang para x86_64-elf (musl) (GCC 14 + binutils, sem musl)
- [ ] CMake toolchain file
- [x] Makefile principal com targets: `kernel`, `userspace`, `iso`, `run`, `debug`
- [ ] Dockerfile para build reprodutível
- [ ] CI GitHub Actions (build + test básico)
- [ ] Scripts QEMU (run, debug com GDB)

**Entregável**: `make run` boota kernel mínimo no QEMU com serial output.

---

### **Fase 1 — Kernel Core** ✅
- [x] Entry point Limine (boot.S) → C kernel_main
- [x] GDT, IDT, ISR/IRQ handlers
- [x] Paging (4-level, higher-half kernel, recursive mapping)
- [x] Physical Memory Manager (bitmap/frame allocator)
- [x] Virtual Memory Manager (map/unmap, kernel heap)
- [x] Kernel heap (bump allocator → slab)
- [x] Timer (PIT → HPET/TSC), ticks, sleep/usleep
- [x] Serial logging (COM1), panic handler, stack trace
- [x] Interrupt-driven keyboard (PS/2) básico

**Entregável**: Kernel boota, mostra log no serial, gerencia memória, trata interrupções.

---

### **Fase 2 — Drivers & Filesystem** 🔶 em andamento
- [x] Framebuffer driver (Linear framebuffer via Limine/UEFI GOP) (24 e 32 bpp)
- [ ] PCI enumeration, BAR mapping
- [x] PS/2 mouse + USB HID (teclado/mouse) (PS/2 pronto; USB pendente)
- [x] VFS layer (mount, open/read/write/close, opendir/readdir) (ramfs gravável + montagem de FS reais)
- [x] FAT32 driver (boot partition, USB) (somente leitura, em teste; driver de disco ATA e partições MBR/GPT prontos)
- [x] ext2/ext3/ext4 driver (root partition) (somente leitura; extents, htree, symlinks)
- [x] NTFS (somente leitura; sem arquivos comprimidos/criptografados)
- [x] Ramfs/tmpfs para /dev, /run, /tmp (`/tmp` pronto; /dev e /run pendentes)
- [ ] Device nodes (/dev/fb0, /dev/kbd, /dev/mouse, /dev/null, /dev/zero)

**Entregável**: Kernel monta partitions, lê arquivos, framebuffer funcional, input funcional.

---

### **Fase 3 — Userspace Básico** ✅
- [x] Syscall infrastructure (sysenter/syscall, sysret/iretq)
- [x] Process/thread creation (fork, execve, exit, wait)
- [x] ELF loader (userspace binaries)
- [ ] Minimal libc (musl fork ou nova: syscalls, malloc, string, stdio, pthread) — `ulib` mínima pronta (syscalls, string, printf)
- [x] Init (PID 1): monta fs, lança getty/login, inicia WM
- [x] Shell básico (builtins: cd, ls, cat, echo, export, exec)
- [x] Userspace heap, mmap, brk (sbrk pronto; mmap pendente)

**Entregável**: Boota para shell interativo no framebuffer com teclado.

---

### **Fase 4 — Graphics Stack & Window Manager** 🔶 funcionando, faltam itens
- [x] libgui: janelas, widgets (button, label, input, scroll), layout (flex/grid) (básico: janela, texto, botão)
- [ ] Font rendering (stb_truetype ou FreeType minimal)
- [x] Compositor: double/triple buffer, damage tracking, vsync (back buffer + damage tracking; sem vsync)
- [x] WM: protocolo IPC (shared memory + event ring buffer) (syscalls + fila de eventos)
- [ ] Window management: tiling (master/stack), floating, workspaces
- [x] Decorações: título, botões (close/max/min), resize handles (título, fechar e minimizar; sem maximizar/resize)
- [x] Input routing: focus follows mouse, click-to-focus, keybindings (click-to-focus)

**Entregável**: WM roda, abre janelas, redimensiona, troca workspaces, compositor vsync.

---

### **Fase 5 — Desktop Environment** 🔶 iniciada
- [x] Panel: clock, system tray, workspace switcher, app menu button (relógio e botão do menu; sem tray)
- [x] Application launcher (menu estilo start / rofi-like) (menu Iniciar configurável em `/etc/menu`)
- [ ] Desktop icons: grid, drag-drop, context menu
- [ ] Notifications daemon (toast, persistência, ações)
- [ ] Settings: wallpaper, theme, keybindings, network, display
- [x] Session management: logout, reboot, shutdown via syscalls (desligar e reiniciar pelo menu, ACPI real; sem logout)
- [ ] Theme engine: CSS-like ou estruturado (colors, radii, fonts)

**Entregável**: Desktop usável com painel, menu, ícones, notificações, temas.

---

### **Fase 6 — Aplicações Core** 🔶 versões básicas prontas
| App | Features Mínimas |
|-----|------------------|
| **File Manager** | Tree view, list/icon view, copy/move/delete/rename, mime types, bookmarks, address bar, thumbnails |
| **Image Viewer** | Zoom/pan, fit/window, next/prev, slideshow, EXIF, formatos: PNG, JPEG, BMP, GIF, WebP |
| **Text Editor** | Syntax highlight, line numbers, tabs, search/replace, undo/redo, encoding, large files |

- [x] Cada app como binário ELF independente (term, notepad, files, calc, taskmgr)
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
## Creditos

- Fonte de texto: Terminus 8x16 (SIL Open Font License 1.1, (c) Dimitar Zhekov), convertida por `tools/gen_font.py`.
- Fonte 8x8 antiga: `font8x8_basic` (dominio publico).
- Bootloader: Limine.
