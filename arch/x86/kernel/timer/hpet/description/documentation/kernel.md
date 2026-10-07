# HPET discovery collegata al kernel — 2026-10-06

Aggiornamento 2026-10-07: aggiunto [probe con mapping temporaneo](probe.md).
Questa pagina conserva i risultati della sola discovery precedente.

Il kernel chiama x86_hpet_kernel_capture dopo kernel_acpi_initialize e prima
di MADT/APIC/device test. La directory viene rivalidata con il profilo fisico
del pool; tutte le letture passano per x86_page_window_read_acpi. Il risultato
FOUND viene copiato nella descrizione statica del kernel solo dopo i controlli
e il completamento della diagnostica. Non viene eseguito MMIO HPET.

## Stato e contratto

- Output persistente, aligned e disgiunto da context/window/directory/pool.
- Page window inattivo prima e dopo, stesso root/pool e FreePages invariato.
- Nessun mapping o comparator HPET acquisito, nessuna allocazione aggiunta.
- HPET assente: KERNEL_HPET_ABSENT, output invariato e boot consentito.
- Directory, tabella, lettura o cleanup invalidi: ritorno failure e panic
  HPET_DISCOVERY_INVALID dal caller, senza pubblicare una descrizione.
- FOUND: dati e KERNEL_HPET_DISCOVERY_OK, poi il resto del boot ordinario.

La lista ACPI resta immutabile e il reader appartiene al kernel. L'adapter
non copia buffer utente e non offre recupero da fault su puntatori arbitrari.
Nessun risultato SMEP/SMAP/KPTI e dedotto da questo componente.

R-001 resta aperto: mapping MMIO, capabilities reali, comparator, routing IRQ
e recupero di HLT da perdita dell'IRQ LAPIC non sono ancora implementati.
La descrizione persistente prepara tali passi e non sostituisce ownership
del device o controllo delle sue capabilities.

## Prove eseguite

Host/component: `scripts/test.ps1`, evidenze
`kernel/build/hpet-description-1aefb7f79618452a9326645afd7b61d9/`.
Tre test Rust, suite C decoder/discovery/adapter per 100 ripetizioni con
ASan e dieci avvii UEFI delle fixture decoder/discovery: tutti passati.
Adapter host con mock delle API kernel: FOUND, ABSENT, directory invalida,
errore read, window ancora attivo, FreePages alterato, root alterato,
errori di logging, root iniziale incoerente, null e alias output/directory.
L'ASan strumenta C host, non Rust e non kernel freestanding. L'accounting
fisico e il controllo del page window sono verificati a runtime nel kernel;
non viene dichiarata una misura Valgrind di leak RAM fisica.

Kernel reale: `scripts/kernel.ps1`, evidenze
`kernel/build/hpet-kernel-34157dc9a0644075a3ae21e7c92ac3d6/`.
Build C11 /W4 /WX e Rust -D warnings, ELF convertito/verificato, ESP isolata
con loader UEFI e manifest SHA-256 delle due immagini e dei sorgenti.

Dieci boot consecutivi QEMU q35/TCG/OVMF raggiungono KERNEL_READY e
KERNEL_HPET_DISCOVERY_OK. La tabella e la directory sono quelle reali del
firmware QEMU. In questo profilo: physical 0xfed00000, ID 0x8086a201,
numero 0; nessuno di questi valori e imposto dal codice di produzione.
Il monitor legge la descrizione persistente e la confronta con i log;
sono conservati anche registri CPU, seriale e trace int/cpu_reset.
Nessun triplo fault o errore inatteso osservato fino al completamento.

Undicesimo boot q35,hpet=off: KERNEL_HPET_ABSENT e KERNEL_READY,
nessun falso KERNEL_HPET_DISCOVERY_OK. Tutti gli undici boot passati.
Non sono prove su hardware fisico o sul watchdog IRQ.

## Review finale

Nessuna allocazione nel nuovo adapter. Dipendenze solo dalle interfacce
pubbliche ACPI, page window e logging; include senza ciclo. Sorgenti del
modulo sotto 400 righe, cartelle al massimo quattro file diretti;
kernel/main.c resta a 399 righe. Stile/diff verificati senza refactor globale.

Cppcheck 2.22.0 sull'adapter e sul test: nessun error/warning/performance.
XML conservato nelle evidenze. Segnalazioni style residue: controlli di
root/pool/FreePages classificati invarianti dal modello statico del callback,
const e nomi parametri dei mock, loop di azzeramento e entry test invocata
dal runner generato. Root/FreePages/Active alterati sono realmente iniettati
nei test host e rifiutati; i controlli runtime non sono rimossi per soddisfare
l'analizzatore. Informazioni sugli header standard non forniti sono distinte
da errori di compilazione. Nessuna coverage percentuale misurata.

Le copie della stessa immagine ELF verificata possono essere usate nella
ESP ordinaria generata. Nessuna scrittura a boot firmware o disco fisico.
