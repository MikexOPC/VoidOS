# HPET probe in sola lettura — 2026-10-07

Il blocco operativo successivo e descritto nella
[chiusura R-001](../../watchdog/documentation/validation.md); questa pagina
conserva le prove preparatorie del solo probe.

Aggiornamento successivo: [capacita dei comparatori](comparators.md),
con selezione osservativa e prove sull'immagine aggiornata. Le evidenze
riportate sotto descrivono il probe precedente, limitato a ID e periodo.

Il percorso kernel esegue discovery, preflight e probe prima di pubblicare
la descrizione persistente. Mapping temporaneo UC, supervisor, read-only e
NX attraverso il servizio di borrow esistente; revoca prima del ritorno e
verifica dell'inventario paging. Nessun nuovo mapping permanente, allocazione,
reset del counter, scrittura a comparator o attivazione IRQ.

R-001 resta aperto: il probe non rende terminabile HLT. Prossimo blocco:
ownership comparator, routing supportato e deadline indipendente, con
iniezione della perdita dell'IRQ LAPIC.

## Design e fonti

```text
validate directory; copy HPET table; stage description
verify current RAM paging inventory
reject RAM/free/live/table-page/leaf aliases of the full HPET page
check current CPU/PAT; choose a strong UC entry
borrow one page read-only; read static capability register twice
revoke temporary mapping and verify ordinary paging inventory
validate ID against ACPI and period; log; publish description
```

Profilo iniziale: base 4KiB aligned, intera pagina entro limite fisico CPU.
Una regione firmware sovrapposta deve essere Reserved o MMIO con attributi
compatibili UC, coprire la pagina e non essere duplicata. Un gap nella mappa
UEFI e ammesso soltanto per l'indirizzo derivato dalla tabella ACPI validata,
dopo rifiuto di RAM/lease/alias. Non esiste un'eccezione per 0xfed00000.
Non e autenticazione della tabella o protezione contro firmware malevolo.

[Intel HPET 1.0a](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf),
sezioni 2.3.2/2.3.4: letture dword a 0x00 e 0x04, stessa coppia riletta.
ID bits 31:0, period bits 63:32; periodo nonzero <= 100000000 fs,
NUM_TIM_CAP bits 12:8 e indice dell'ultimo comparator, quindi conteggio +1.
Il periodo misurato dal registro non prova che il counter sia in moto.

[Intel SDM Vol.3A, dicembre 2024, sezioni 13.12.2/13.12.3](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf):
PAT UC encoding 0; PWT bit 3, PCD bit 4, PAT bit 7 per foglia 4KiB.
Servizi CPU/mapping esistenti verificano prerequisites e invalidano il TLB.
Nessun accesso a registri riservati; volatile e letture read-only, non lock SMP.

## Prove sullo stato finale

Host/component: `scripts/test.ps1`, evidenze
`kernel/build/hpet-description-a68a4c9e7cca4c36bd877e2a88a0f89f/`.
Quattro test Rust passati. Per 100 ripetizioni: suite decoder/discovery/adapter
con ASan e suite probe C con ASan. Probe host usa vero preflight e callback
C con mock CPU, memory map, paging e borrow; non esegue istruzioni privilegiate.
Dieci avvii UEFI delle fixture decoder/discovery passati.

Negative probe: RAM, free/live leases, alias foglia/table page, ID errato,
periodo zero/fuori limite, borrow failure, revoca fallita, inventario finale
invalido, logging failure, IF=1, physical bits incoerenti, PAT index invalido,
base non allineata e overflow. Adapter rifiuta output alias dell'inventario.
Nessuna allocazione; ASan host non misura leak di RAM fisica ne strumenta Rust.

Kernel: `scripts/kernel.ps1`, evidenze
`kernel/build/hpet-kernel-5c780594266b45a58d640baa8ef8320d/`.
Loader ricostruito con -BootKernel e suite host C/Rust del boot superata.
Dieci boot reali q35/TCG/OVMF: KERNEL_HPET_PROBE_OK,
KERNEL_HPET_DISCOVERY_OK e KERNEL_READY, nessun errore/triplo fault inatteso.
Periodo osservato 10000000 fs e tre comparators, non valori imposti nel codice.
Undicesimo boot hpet=off: ABSENT e READY, nessun falso PROBE_OK.
Manifest delle immagini/sorgenti, log serial/debug/monitor e registri CPU
conservati; descrizione persistente confrontata tramite monitor con i log.

Cppcheck 2.22.0 enable-all/inconclusive/exhaustive su probe/adapter e mock:
nessun finding error/warning/performance, XML nelle evidenze kernel. Style/info
residui conservati e distinti da warning compiler; riguardano soprattutto
API/mock e callback non modellati interamente. C /W4 /WX e Rust -D warnings.
Diff/stile/limiti verificati; main.c 399 righe, modulo al massimo quattro file
diretti per cartella. Nessuna percentuale di coverage misurata.

Il gate QEMU passa per questo probe, non per hardware fisico o watchdog IRQ.
