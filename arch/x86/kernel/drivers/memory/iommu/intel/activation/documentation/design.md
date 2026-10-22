# Attivazione VT-d e fault primari: ricerca e design (blocco 2)

22 ottobre 2026. Documento di progetto. Decisioni utente ricevute: D1-C'
(negare per default), D2 polling, D3 `edu` per il blocco 3, D4 attivazione
temporanea. Implementazione e prove: [validazione](validation.md). Segue il
[routing GPU–DRHD validato](../../routing/documentation/validation.md) e usa i
moduli gia verificati: [registri](../../registers/documentation/validation.md),
[dominio privato](../../domains/documentation/validation.md) e
[coerenza CPU e QI](../../invalidation/documentation/validation.md).
Le scelte **D1–D4** sotto riportano le alternative discusse.

## Obiettivo e perimetro

Pubblicare la root del dominio privato sull'unita scelta dal routing,
attivare la traduzione (TE), osservare i fault con il logging primario e
disattivare in modo verificato, nel solo profilo diagnostico QEMU. Il blocco
non concede ancora DMA alla GPU e non porta VT-d nella console ordinaria:
le prove di DMA consentito/vietato e revoca sono il blocco 3.

Moduli toccati, come implementati: nuovo `activation/` con sequenza
(`sequence.c`), fault (`faults.c`), accessore MMIO con whitelist propria
(`backend/mmio.c`) e policy Rust pura (`rust/`). I fault restano nello stesso
modulo invece di una cartella `faults/`: condividono owner, whitelist e
stato. `registers/` e `domains/` non cambiano: il mapping RW resta limitato a
una unita, quindi l'attivazione richiede `route->Unit==0` con una sola unita
descritta. `invalidation/` riceve un adattatore: il campo `Status` dichiara
quali bit GSTS (TES31, RTPS30) appartengono all'attivazione, la coda li
tollera accanto a QIES e rifiuta lo stop finche TES puo essere attivo.

## Fonti verificate

Intel VT-d rev5.0 D51397-017, copia locale con SHA256
`A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`
(kernel/build/iommu-research/vtd-831418.pdf):

- §11.4.4.1 GCMD_REG, offset 018h, 32 bit WO. TE bit31, SRTP bit30, QIE bit26.
  Un comando alla volta: leggere GSTS, azzerare i bit one-shot con la maschera
  `96FFFFFFh`, impostare o azzerare il bit, scrivere GCMD, attendere il bit
  corrispondente in GSTS. SRTP deve precedere ogni abilitazione di TE.
  L'abilitazione avviene a confini di transazione deterministici.
- §11.4.4.2 GSTS_REG, offset 01Ch: TES bit31, RTPS bit30, QIES bit26.
- §11.4.5 RTADDR_REG, offset 020h, 64 bit: RTA bit63:12 (root 4 KiB
  allineata), TTM bit11:10 (00b = tabelle legacy, quelle del dominio privato).
- §6.6: con CAP.ESRTPS (bit63) a 0, dopo SRTP il software invalida
  globalmente, nell'ordine, context-cache e IOTLB (PASID/HPT non applicabili
  al profilo legacy). Con ESRTPS a 1 lo fa l'hardware.
- §6.8 e CAP.RWBF (bit4): flush del write buffer solo se richiesto
  dall'implementazione; QEMU dichiara RWBF=0, il percorso resta rifiutato
  finche non e implementato.
- §7.2.1: logging primario. Indice interno azzerato quando TES e IES sono
  entrambi a 0; PPF e l'OR dei bit F; overflow PFO; il software processa i
  record in FIFO circolare da FRI finche trova F=0, poi azzera F e PFO.
- §11.4.7.1 FSTS_REG offset 034h: PFO bit0 RW1CS, PPF bit1 ROS, FRI bit15:8;
  IQE/ICE/ITE gia gestiti dal modulo QI.
- §11.4.7.2 FECTL_REG offset 038h: IM bit31 (default 1, interrupt mascherato),
  IP bit30.
- §11.4.7.6 FRCD_REG[n]: 128 bit, allineati a 16; F bit127 RW1CS, T1 bit126,
  FR bit103:96, SID bit79:64, FI bit63:12. Posizione: CAP.FRO (bit33:24) x16;
  numero: CAP.NFR (bit47:40) + 1. Le capacita sono gia lette e validate dal
  modulo registri (`FaultOffset`, `FaultCount`).

Confronto di metodo, nessuna copia: Linux v6.18 `drivers/iommu/intel/iommu.c`
(`iommu_set_root_entry`, `iommu_enable_translation`) e `dmar.c`
(`dmar_fault`). QEMU v11.1.0 `hw/i386/intel_iommu.c` e il modello emulato,
non una garanzia hardware.

## Stati

    VERIFIED_PRIVATE (dominio) + QI_IDLE + ROUTE_FOUND + FAULTS_CLEAN
      -> ROOT_WRITTEN        RTADDR scritto e riletto
      -> ROOT_PUBLISHED      SRTP servito (RTPS=1)
      -> CACHES_INVALIDATED  QI: context globale, IOTLB globale, wait
      -> TRANSLATING         TE servito (TES=1), FSTS ricontrollato
      -> DRAINED             TE=0 servito (TES=0)
      -> CACHES_INVALIDATED' QI globale dopo la disattivazione
      -> UNPUBLISHED         RTADDR azzerato (senza nuovo SRTP)
      -> VERIFIED_PRIVATE    rilascio del dominio permesso

Nessun secondo SRTP: RTPS e persistente (in QEMU resta a 1 fino al reset),
quindi un secondo SRTP non sarebbe osservabile. Con TES=0 l'hardware non
cammina le tabelle e §11.4.4.1 impone un SRTP prima di ogni nuova
abilitazione: il puntatore latched non autorizza traduzioni future.
Per lo stesso motivo l'attivazione richiede in ingresso GSTS uguale al solo
QIES: un RTPS gia impostato renderebbe il primo SRTP non verificabile.

Nessuno stato intermedio libera RAM. Un comando non servito entro la
deadline, un fault inatteso o una rilettura diversa portano a RETAINED: il
dominio, le tabelle, la coda QI e il mapping registri restano posseduti e
nessuna pagina torna all'allocatore, perche l'hardware potrebbe ancora
leggerle. RETAINED e terminale per la sessione diagnostica.

Le deadline usano il clock PM gia validato (un secondo e un tetto di letture),
come nel modulo QI: sono limiti del diagnostico, non latenze hardware.

## Fault

Primo profilo: polling con FECTL.IM lasciato a 1 (nessun interrupt). Prima
di SRTP il modulo legge FSTS: PPF o PFO gia impostati sono un rifiuto,
perche non si possono attribuire. Durante e dopo TE il lettore scorre i
record da FRI, copia SID/FR/T1/FI in un buffer limitato posseduto dal
chiamante, azzera F, poi PFO. Un record non atteso (per esempio un SID
diverso dal requester previsto) e un errore di attivazione.

Gli interrupt di fault (FEDATA/FEADDR verso il LAPIC, nuovo vettore e
nuovo handler) sono rinviati: richiedono il percorso IRQ con le stesse
regole di ownership/EOI degli altri vettori.

## Decisioni richieste

**D1 — Sorte degli altri requester sotto la stessa unita.** TE agisce su
tutta l'unita: ogni funzione senza context entry vedra le sue DMA
rifiutate (fault "context non presente"). Opzioni:

- A (consigliata): prima di SRTP leggere, solo lettura via ECAM, il bit Bus
  Master Enable (registro Command offset 04h, bit2, PCI Local Bus 3.0
  §6.2.2: offset e bit da riverificare sulla fonte primaria prima del codice)
  di tutte le funzioni coperte dall'unita nello snapshot topologico. Se una
  funzione diversa dal requester previsto ha BME=1, l'attivazione e
  UNSUPPORTED: niente scritture ai device, niente traduzione.
- B: azzerare BME degli altri device. Introduce scritture di configurazione
  su device non posseduti: rinviato.
- C: attivare comunque e registrare i fault. Rischia di rompere device
  ancora attivi lasciati dal firmware: sconsigliata.

**Misura del 22 ottobre (dopo l'approvazione di D1-A).** Fonte del bit:
Linux v6.18 `include/uapi/linux/pci_regs.h`, `PCI_COMMAND 0x04` (16 bit) e
`PCI_COMMAND_MASTER 0x4`; QEMU v11.1.0 `hw/pci/pci.c` (`pci_set_master`)
apre o chiude l'intero spazio DMA del device in base a quel bit. Un boot
Q35/OVMF con l'immagine routing finale, letto via qtest dall'ECAM dopo
KERNEL_READY (`kernel/build/bme-probe-*`), mostra BME=1 su **tutte** le
funzioni del bus0: 00:00.0 host bridge, 00:01.0 GPU, 00:1f.0 LPC,
00:1f.2 AHCI, 00:1f.3 SMBus. Con D1-A l'attivazione sarebbe quindi sempre
UNSUPPORTED sul profilo QEMU di riferimento: la scelta va rivista.

- C' (nuova proposta): negare per default. Nessuna context entry per gli
  altri requester, nessuna scrittura ai loro registri; i fault con SID
  diverso dal requester posseduto vengono registrati come DMA bloccati e
  non sono un errore. Un fault del requester posseduto resta un errore.
- B resta possibile: azzerare BME degli altri device prima di TE, con
  scritture di configurazione su device non posseduti.

**D2 — Fault: polling (consigliato) oppure interrupt subito.**

**D3 — Requester delle prove.** Questo blocco attiva TE con il solo dominio
previsto; il blocco 3 deve generare DMA controllati. Il device QEMU `edu`
(disponibile in questa installazione, `-device edu`) offre un motore DMA
semplice e deterministico, utile per accessi consentiti, vietati e dopo la
revoca, indipendente da VirtIO. In alternativa si usa direttamente la GPU
VirtIO. Consigliato `edu` per il blocco 3; la GPU nel blocco 4. Nota:
in QEMU VirtIO aggira l'IOMMU se `iommu_platform=on` non e impostato e
VIRTIO_F_ACCESS_PLATFORM non e negoziato: senza entrambi una prova DMA sulla
GPU non dimostrerebbe nulla.

**D4 — Durata.** Nel diagnostico: attivare, verificare, disattivare e
rilasciare, come il modulo QI (consigliato). TE persistente nella console
ordinaria arriva solo dopo i blocchi 3 e 4.

## Rischi

- Disattivazione: §11.4.4.1 non garantisce che le cache non contengano
  traduzioni vecchie; per questo dopo TE=0 si invalida di nuovo e solo dopo
  si riusa la RAM delle tabelle.
- ECAP.C=0 in QEMU: ogni modifica alle tabelle passa dal flush CPU del modulo
  QI prima di pubblicarla. Root e context gia verificati restano immutati
  mentre TES=1.
- Un fault osservato non prova che il blocco funzioni anche per accessi
  consentiti: quello e il blocco 3.

## Accettazione

1. Host C/ASan e Rust: sequenza con registri simulati, ogni comando non
   servito, rilettura RTADDR diversa, FSTS sporco, record FIFO con wrap e
   overflow, SID inatteso, RETAINED senza rilascio, nessuna scrittura fuori
   whitelist. Warning come errori, Clippy, cppcheck.
2. QEMU (`intel-iommu`, legacy, caching-mode on): dieci boot con trace
   `vtd_reg_write` limitata a RTADDR/GCMD/FSTS/FRCD e alle scritture QI note;
   il verifier legge GSTS/RTADDR in TRANSLATING e dopo UNPUBLISHED; nessun
   fault inatteso; budget fisico ripristinato. Negativi nel modello host
   (FSTS sporco, RTPS gia impostato, comandi non serviti, fault del requester).
3. Regressioni: routing, QI, topologia, console ordinaria (dieci boot);
   audit dei simboli: nessun simbolo `activation/` o `faults/` nell'immagine
   ordinaria.

Nessuna di queste prove dichiara isolamento DMA: serve il blocco 3.
