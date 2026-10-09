# Base del kernel

Stato corrente: [docs/status.md](../status.md). Questo file descrive
ingresso, inventario e log; non elenca piu l'intero perimetro aperto.

Il kernel vive in `kernel/` e `arch/x86/kernel/`, separato dal probe in
boot/uefi/test/kernel. Non usa servizi UEFI e non ritorna al bootloader.
Gestisce [ownership fisica](memory/physical-allocator.md), una finestra
RW/NX e stack propri protetti da guardie. Scheduler cooperativo BSP,
interrupt esterni e timer sono nel percorso di avvio. Non e un kernel
ibrido: ring 3, syscall e spazi utente restano fuori.

## Costruzione e prove

```powershell
.\arch\x86\boot\uefi\scripts\build.ps1 -BootKernel
.\kernel\scripts\build.ps1 -Install
.\kernel\test\host.ps1
.\kernel\test\image.ps1
.\kernel\test\firmware.ps1 -UseInstalled
```

La build del bootloader mette il vecchio probe nella ESP; eseguire la
build kernel con -Install dopo di essa sostituisce kernel.elf nella ESP
generata. I test QEMU usano ESP isolate e non sostituiscono tale immagine.
`-UseInstalled` prova esattamente l'ELF presente nella ESP ordinaria;
senza opzioni il test ricostruisce anche il kernel normale nella propria fixture.
`-TestPanic` costruisce soltanto la variante sacrificabile; -Install la rifiuta.
Artefatti e log del kernel sono in kernel/build/, esclusa da Git.

La toolchain attuale e MSVC x64 piu rustc per x86_64-pc-windows-msvc.
Il kernel usa l'ABI Microsoft x64 previsto dal trampoline: RCX contiene
il contratto, stack allineato e shadow space sono predisposti dal loader.
Il link non usa CRT, import, TLS o rilocazioni. Il convertitore limitato
trasforma il PE locale in ELF64 statico, mantenendo indirizzi e BSS.
Permessi delle sezioni sono R/X, R o RW; codice RW/X viene respinto.
I metadati .pdata rimangono R e non costituiscono un servizio di unwind.

## Ingresso e proprieta

L'ingresso specifico x86 controlla l'indirizzo del contratto prima di
dereferenziarlo e legge CR3 realmente attivo. Il codice comune verifica
firma, versione 2, campi riservati, caratteristiche, root, ingresso, stack
e geometria della fotografia. Conserva una copia di 112 byte del contratto
e un prestito immutabile della mappa, nella propria BSS.

Il contratto numerico viene riusato dall'header del bootloader, senza
duplicare una seconda definizione ABI. I buffer del loader restano vivi:
nessun FreePages, liberazione del codice di transizione, cambio di root
o reclamazione firmware viene eseguito dal kernel.
Le API C richiedono buffer leggibili della dimensione dichiarata e output
posseduti e distinti. I controlli numerici non rendono leggibile un puntatore
arbitrario; il percorso reale usa le mappature verificate dal bootloader.

## Inventario della memoria

C legge i descrittori byte per byte usando lo stride dichiarato, senza
struct packed, allineamento implicito o sizeof assunto per il firmware.
Rust no_std verifica moltiplicazione, somma, allineamento, limite fisico
di 52 bit e sovrapposizioni attraverso FFI di soli scalari, senza puntatori,
heap o blocchi unsafe. L'output viene pubblicato dopo verifica dell'intera mappa.

Soltanto ConventionalMemory con attributi di cache standard e base non
nulla viene conteggiata come candidata. I bit UC/WC/WT/WB/UCE possono
coesistere come capacita e non provano la politica di cache attiva.
Altri attributi, tipi sconosciuti, loader, boot services, runtime e MMIO
restano riservati. Un intervallo che parte da zero resta interamente
riservato. Le risorse del kernel, stack, tabelle, contratto, mappa e
transizione restano quindi escluse attraverso i tipi LoaderData/LoaderCode.

Il conteggio riservato include spazio MMIO: non rappresenta RAM consumata.
Il conteggio candidato non implica assegnazione o mappatura.
L'allocatore fisico seleziona un sottoinsieme con capacita WB e entro il
limite CPUID, registra assegnazioni e rilasci. Queste non sono puntatori
scrivibili. La [finestra iniziale](memory/page-window.md) mappa fino a 16
pagine contigue RW/NX, le azzera, ne prova il riuso e ripristina il budget
fisico prima di KERNEL_MAPPING_OK e KERNEL_WINDOW_16_PAGES_OK.
La reclamazione del loader resta un passo successivo.
Il [pool iniziale di oggetti](memory/object-pool.md) usa poi il blocco
mappato per verificare allineamento, oggetti su piu pagine, ID e riuso.
Libera gli oggetti e scollega il buffer prima di rimuovere il mapping;
KERNEL_OBJECTS_OK conferma anche il ripristino del budget fisico.
I [mapping simultanei](memory/region-mappings.md) aggiungono poi quattro
slot indipendenti; due blocchi da 128 e 64 KiB rimangono vivi, RW/NX e
protetti da guardie, senza dipendere dalla finestra temporanea.
Il [pool persistente](memory/persistent-objects.md) usa il secondo blocco
per contesto di boot e inventario: 144 byte di oggetti, con prestito del
mapping mantenuto fino allo scollegamento del pool. La continuazione
li verifica nuovamente dopo aver revocato lo stack del loader.
Il confronto a coppie usa O(n^2) tempo, nessun heap e al massimo 4096 regioni;
e una scelta iniziale limitata, da misurare prima di estenderne l'uso.

## Diagnostica e panic

COM1 a 0x3f8 e un profilo diagnostico QEMU esplicito, non discovery di
hardware generico. Il log di bootstrap usa UART 16550, 115200 baud/8N1
e trasmissione polling con 100000 tentativi massimi per carattere.
Coda TX e rotta IRQ UART sono moduli successivi; non sostituiscono
questo percorso di panic. UART assente o bloccata fa fallire la scrittura;
non provoca attesa infinita. I testi sono statici, limitati e senza printf.

I separatori sono su righe distinte con una riga vuota; solo le etichette
ANSI ricevono colore. Successo viene emesso dopo ogni verifica.
Panic stampa la ragione se possibile e arresta autonomamente con CLI/HLT,
conservando in RAX il marker 0x4b50414e49430001. L'avvio completato usa
0x4b52454144590001. Nessun percorso ritorna al firmware.

GDT/IDT/TSS ed eccezioni sono gestiti dal kernel, con tre IST distinti
e stack principale autonomo: [descrittori](descriptors/ownership.md) e
[stack](stack/main-stack.md). Il mapping del vecchio stack e revocato,
le sue pagine fisiche restano riservate. Restano ereditate le tabelle
paging e lo stato SIMD/FPU. Interrupt APIC, UART IRQ, LAPIC e HPET sono
nel percorso ordinario. Mancano diagnostica grafica, recupero dalle
eccezioni, SMP, user mode e prove su hardware fisico.
`kernel/main.c` (400 righe) esegue ancora prove di mapping e oggetti
prima dei servizi: i controlli di ownership restano; la diagnostica di
riuso va separata dal bootstrap.

Riferimenti: [UEFI 2.11, memoria](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html),
[UART TI TL16C550C](https://www.ti.com/lit/ds/symlink/tl16c550c.pdf),
[QEMU invocation](https://qemu-project.gitlab.io/qemu/system/invocation.html)
e [transizione del loader](../handoff/transition/activation.md).
