# VoidOS

VoidOS è un sistema operativo sperimentale x86-64 con un bootloader UEFI
proprio e un kernel scritto in C, Assembly e Rust. Il progetto punta a
eseguire programmi isolati, caricati da disco, su tutte le CPU.

Il profilo Meson `ordinary` avvia il kernel, inizializza la memoria e i core
secondari e, con SMEP e SMAP disponibili, esegue un programma Rust
incorporato in ring 3. Con un controller PS/2, `ordinary` apre la shell
con tastiera e mouse e può interrogare il servizio `bridge` in ring 3.
Senza controller, il BSP entra in idle con gli interrupt abilitati. Lo
[stato del progetto](docs/status.md) riporta le capacità e i limiti di ogni
profilo.

## Cosa non funziona ancora

La shell gira ancora in una sessione sul BSP. Mancano uno scheduler
preemptive permanente su tutte le CPU, driver a blocchi, VFS, esecuzione
di ELF da file, entropia e stack canary. Vedi lo [stato del progetto](docs/status.md).

## Prerequisiti su Windows x64

- PowerShell 7, Python, Meson 1.12.1 o successivo e Ninja. Meson, Ninja e
  `rustc` devono essere nel PATH.
- Visual Studio 2022 Build Tools con gli strumenti C/C++ e MASM x64 nella
  posizione attesa `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC`.
  Il kernel e il loader scelgono la versione più recente in quella cartella
  (14.44.35207 nella prova). Servono gli header UCRT del Windows SDK sotto
  `C:\Program Files (x86)\Windows Kits\10\Include` (10.0.26100.0 nella prova).
- Rust per `x86_64-pc-windows-msvc` con il target aggiuntivo
  `x86_64-unknown-none` (rustc 1.98.1 nella prova).
- QEMU x86-64 con OVMF (QEMU 11.1.0 nella prova). Il runner cerca
  `qemu-system-x86_64.exe` in `C:\Program Files\qemu` e i file
  `edk2-x86_64-code.fd` e `edk2-i386-vars.fd` nella sottocartella `share`.
  Per un'installazione altrove, il runner accetta `--qemu-dir`.
- NASM 3.01 va nella cache `kernel/build/tools/nasm-3.01` del checkout.
  Il comando seguente scarica il pacchetto fissato da nasm.us e ne verifica
  gli hash.

## Compilazione e avvio

Da PowerShell 7, nella radice del repository, esegui in ordine questi comandi.
Su una build già configurata puoi saltare i due passaggi `meson setup`. Scarica
NASM alla prima preparazione della cache.

```powershell
./arch/x86/kernel/cpu/percpu/startup/scripts/assembler.ps1 -Download
meson setup kernel/build/meson-kernel kernel --vsenv -Dprofile=ordinary
meson compile -C kernel/build/meson-kernel
meson test -C kernel/build/meson-kernel
./arch/x86/boot/uefi/scripts/build.ps1 -BootKernel
meson setup kernel/build/meson-qemu-runner test/tooling/qemu-runner --vsenv
meson compile -C kernel/build/meson-qemu-runner
./kernel/build/meson-qemu-runner/qemu_runner.exe --ordinary --image kernel/build/meson-kernel/kernel.elf --boots 1 --smp 4
```

Meson produce `kernel/build/meson-kernel/kernel.elf` e controlla la parità
con il piano di build. Lo script UEFI compila il loader `BOOTX64.EFI` e ne
esegue i test host. Il runner prepara una ESP con il loader e l'ELF Meson,
avvia una VM senza finestra grafica, controlla i marker seriali e scrive
`results.json` e i log nella directory indicata da `Evidence`.

I sorgenti del kernel sono in [kernel/](kernel/) e [arch/x86/](arch/x86/).
Consulta la [mappa del codice](docs/architecture.md), la [roadmap](docs/roadmap.md) e la [guida agli script](scripts/README.md).
Il codice del progetto è distribuito con licenza [GPL-3.0-only](LICENSE).
Il font Cascadia Mono e i dati derivati del font mantengono la
[SIL Open Font License 1.1](arch/x86/kernel/drivers/video/framebuffer/font/LICENSE).
La [provenienza e l'hash del font sorgente](arch/x86/kernel/drivers/video/framebuffer/font/source/origin.json)
sono registrati insieme al file originale.
