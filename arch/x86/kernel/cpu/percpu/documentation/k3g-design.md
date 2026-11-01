# K3-G — riserva iniziale per il trampolino AP

30 ottobre 2026. Scelta A confermata dall'utente: riserva precoce nel kernel.
Dipendenza necessaria per
completare il gate SMP di [K3-F](k3f-validation.md). In questo blocco nessun
INIT/SIPI, nessun AP attivo e nessuna nuova istruzione privilegiata.

## Fonti verificate

- [Intel SDM Vol.3A, 253668-078US, dicembre 2022](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
  §9.4.4.1 punti 7 e 10: ingresso AP in pagina da 4 KiB sotto 1 MiB;
  il vettore SIPI di otto bit identifica la base fisica come vector << 12.
  La numerazione è §9.4 in questa edizione, non §8.4 citato dalla roadmap
  storica. Nessun indirizzo fisico fisso è deducibile dalla specifica.
- [Linux v6.12, arch/x86/realmode/init.c](https://github.com/torvalds/linux/blob/v6.12/arch/x86/realmode/init.c),
  reserve_real_mode e set_real_mode_permissions: riserva precoce sotto
  1 MiB, preparazione e permessi in momenti distinti. GPL-2.0, riferimento
  concettuale senza copia. Non importiamo la sua riserva dell'intero MiB.
- [UEFI 2.10, §7.2.1 AllocatePages](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-allocatepages):
  alternativa con AllocateMaxAddress prima di ExitBootServices. Il limite
  riguarda l'ultimo byte dell'intervallo, non soltanto la base. L'allocazione
  firmware non stabilisce da sola i permessi delle future PTE del kernel.

## Scelta approvata e alternativa considerata

A, approvata: riserva nel kernel, durante il bootstrap fisico e prima
della prima allocazione generica. Selezione dalla mappa validata e dalle
esclusioni esistenti; nessun nuovo campo nel contratto del loader.
Il punto è stato verificato in kernel/bootstrap/memory.c: subito dopo
initialize/validate dell'allocatore, prima di window e regioni. Una nuova
API circoscritta accetta solo NextId=1, Generation=0 e tutto il budget
ancora libero; prenota e applica il pin sotto lo stesso lock fisico.

B: riserva dal loader UEFI e descrittore esplicito nel contratto boot.
Il kernel verifica range, tipo, durata ed esclusione dall'allocatore.
Prenota prima di qualunque allocazione kernel, ma amplia il blocco a
loader, ABI del contratto e suoi validatori/test. Non elimina il successivo
lavoro di ownership, mapping e transizione AP nel kernel.

## Contratto proposto per A

- Una pagina fisica da 4 KiB per il futuro codice d'ingresso; limite
  inferiore 0x1000, fine esclusiva non oltre 0x100000. Il limite superiore
  viene dal formato SIPI; escludere pagina zero è politica del progetto.
- Solo RAM esplicitamente utilizzabile dalla mappa e non già posseduta o
  esclusa. Firmware/ACPI/MMIO, immagini, stack e altri oggetti vivi non sono
  candidati. Il semplice range numerico non autorizza a scrivere memoria.
- Prenotazione prima dell'esposizione al pool generico, con owner stabile
  e accounting. Niente sottrazione tardiva di una pagina già consegnata.
  La ricerca è finita: al massimo 255 basi candidate, oltre ai limiti già
  dichiarati per i descrittori della mappa.
- In questo blocco la pagina resta inattiva, senza mapping eseguibile e
  senza alias identità nel CR3 operativo. Il codice non viene ancora copiato.
  Scritture di verifica richiedono il mapping posseduto già usato dal progetto.
- Mancanza di RAM idonea: stato SMP_UNAVAILABLE, nessun tentativo AP;
  il normale profilo BSP può continuare, il diagnostico che richiede la
  riserva deve fallire esplicitamente. Mai usare un indirizzo di ripiego.
- Rilascio possibile solo prima del commit del futuro avvio e con owner
  valido. Dopo qualsiasi tentativo hardware, nessun rilascio dedotto da
  un timeout. Il futuro ingresso deve integrare il lifetime K3-E/F.
- C possiede la riserva e il legame al bootstrap; Rust valida geometria,
  overflow e selezione numerica senza dereferenziare indirizzi fisici.
  Nessun Assembly necessario per la sola prenotazione.

Sequenza concettuale, non eseguibile:

    MAP_VALIDATED -> LOW_PAGE_RESERVED -> GENERIC_POOL_AVAILABLE
                         |
                         +-> PREPARED_INACTIVE -> future START_COMMITTED
                         +-> release before commit, with ownership proof

## Perimetro proposto e accettazione

Modulo per-CPU di avvio; soli adattatori necessari al bootstrap fisico,
allocatore, build e test. Prima dell'implementazione leggere quei moduli
per verificare ordine reale, esclusioni e API; aggiornare il progetto se
emerge una scelta architetturale diversa. Test centralizzati in test/.

Test host: estremi, overflow, zero RAM bassa, pagine occupate, frammentazione,
prenotazione singola, duplicati, owner errato e rollback. ASan sul C e test
Rust; accounting distinto dalla misura dei leak fisici.

QEMU: dieci boot finali del profilo positivo; controllo indipendente che
la pagina appartenga alla RAM ammessa, sia esclusa dalle allocazioni e non
crei mapping bassi nel CR3 operativo. Negativo di indisponibilità; dieci
boot ordinari e regressione console pertinente. AP ancora fermi.

Seguono in blocchi distinti: root di transizione e tabelle raggiungibili
dal modo iniziale, blob 16/32/64 bit e gestione fault/NMI precoce, ingresso
privato GDT/TSS/GS, avvio sequenziale AP e prove SMP K3-F. Non assumere
che il CR3 operativo sia caricabile direttamente dal modo protetto; questo
prerequisito deve essere progettato e verificato nel blocco delle tabelle.

## Stato delle verifiche di questo documento

Fonti primarie Intel e Linux lette online; UEFI AllocatePages riscontrato
nel risultato indicizzato ufficiale, pagina diretta non leggibile (HTTP 403).
La ricerca iniziale era solo documentale. L'implementazione aggiunta dopo
la scelta dell'utente e le relative prove sono registrate separatamente
in [k3g-validation.md](k3g-validation.md). Non chiudono il gate SMP K3-F.
