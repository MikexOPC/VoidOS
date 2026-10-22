# VT-d: mapping permanente e lettura capacita

20 ottobre2026. L'utente ha scelto mapping permanente gia in questo blocco,
con i soli adattatori inventario/build/test necessari. Prerequisito:
[discovery DMAR verificata](../../discovery/documentation/validation.md).
Il modulo non abilita traduzione DMA o interrupt remapping.

## Fonti

[Intel VT-d rev5.0 D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf),
11.4 e11.4.1/2/3/4.2: VER0x00/u32 RO, CAP0x08/u64 RO,
ECAP0x10/u64 RO, GSTS0x1c/u32 RO. VER major7:4/minor3:0.
CAP ND2:0, SAGAW12:8 (bit10:4 livelli/48bit), MGAW21:16 codificato N+1,
FRO33:24 in unita16 byte, NFR47:40 codificato N+1.
ECAP C0/IRO17:8; IRO in unita16 byte. GSTS TES31 segnala traduzione attiva.
Leggere non equivale a prendere ownership di uno stato firmware gia attivo.

[Intel SDM3A090US](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf),
5.5.4/5.6: foglie4KiB P0, RW1 spento, US2 spento, XD63; UC dal PAT
verificato, senza global. Nessun valore PAT hardcoded o nuovo assembly.
[QEMU v11.1.0 intel_iommu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/intel_iommu.c)
e relativo header internal sono il riscontro dell'implementazione emulata.

## Ownership e limiti

Un owner non spostabile, fino a8 unita DMAR. Prima pagina registri di
ciascuna unita, UC supervisor RO/NX, una guardia fra pagine e agli estremi.
Ramo PML4[228], tre tabelle da lease pinned. Il mapping e indipendente dal
prestito temporaneo: resta pubblicato finche l'owner non lo revoca.
Registri oltre la prima pagina non vengono letti: offset IOTLB/fault sono
solo decodificati e confrontati con lo span DRHD, non usati come puntatori.

Preflight: geometria DMAR e CPU/root/PAT coerenti; MMIO fuori da RAM,
framebuffer, lease liberi/vivi e alias delle PTE correnti. Vietato adottare
un ramo esistente. Preparare tabelle inattive tramite il prestito WB gia
verificato; pubblicare soltanto dopo readback. Capture completa prima/dopo.
Registro bounded separato: snapshot112 byte, inventario totale3832 byte,
ancora entro l'oggetto4KiB. Nessun registro MMIO generico per altri driver.

Revoca del ramo e flush prima di unregister/unpin/free. Root estranea,
flush incompleto o capture fallita trattengono le risorse, con retry;
mai liberare tabelle ancora raggiungibili. Nessuna allocazione di buffer DMA.
Il bind v0 avviene nel bootstrap, con LAPIC opzionale e prima del video; inventario
sa riconoscere il ramo anche nelle scansioni miste. Runtime IRQ/SMP e
integrazione persistente ordinaria restano blocchi successivi.

## Letture e accettazione

Whitelist dei quattro registri, larghezze naturali e letture ripetute;
nessuna scrittura GCMD o altro registro. Confrontare dati stabili e stato.
Rust puro valida versioni/range e produce un giudizio esplicito per il
profilo legacy futuro; capacita osservate non equivalgono a dominio pronto.
Non interpretare campi non necessari o inventare una maschera universale
dei bit riservati valida per tutte le revisioni VT-d.

Diagnostico: bind, capture, letture, unbind e budget; poi rebind/letture/
unbind. Questo esercita il mapper permanente senza lasciarlo ai successivi
moduli di boot. Test host con allocatore reale e MMU/MMIO simulate, errori
di allocazione/pin/pubblicazione/flush/cleanup, alias e RO/NX/guardie.
Dieci boot QEMU consecutivi con letture confrontate via QTest e tabelle
ritirate verificate, assenza IOMMU e regressioni dei mapping esistenti.
Nessun isolamento DMA, attivazione, IOMMU fisica o supporto SMP dichiarato.

## Esito delle capacita emulabili

Nel profilo QEMU verificato ECAP vale 0xF42: C (bit 0) e zero. Il mapper e
il lettore funzionano, ma il candidato coerente ristretto restituisce
`Compatible=0`. Non e un errore di lettura e non autorizza l'attivazione.
Il progetto delle tabelle DMA dovra includere la visibilita delle scritture
alle strutture di traduzione e le invalidazioni richieste dalla specifica;
un compiler barrier o la policy UC dei registri non risolvono questo requisito.
Le prove di questo blocco sono nel [report di validazione](validation.md).
