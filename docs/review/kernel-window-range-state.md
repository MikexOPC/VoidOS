# Checkpoint: finestra kernel da 1 a 16 pagine

Revisione del 3 ottobre 2026, base Git c5204b5. Task limitata al mapper
della finestra iniziale e ai suoi consumatori, test e profilo del loader.
Non sono stati aggiunti heap, scheduler, driver o nuove tabelle.

## Comportamento verificato

Il mapper accetta un conteggio da 1 a 16, assegna un solo blocco fisico
contiguo e lo blocca contro il rilascio ordinario. Rust verifica la
dimensione prima dell'allocazione e il limite fisico prima della
pubblicazione. L'FFI scambia solo interi, senza puntatori o blocchi unsafe.

Tutte le foglie sono supervisor RW/NX; ogni pagina riceve INVLPG dopo
la pubblicazione e dopo unmap. Prima di togliere una sola foglia si
verificano tutte le PTE, l'identita della lease, il pin e la coda non
utilizzata. La corruzione dell'ultima foglia respinge quindi il rilascio
prima di modificarne la prima. Solo dopo unmap e invalidazioni si
restituisce il blocco all'allocatore fisico.
Il test di avvio annulla il puntatore preso in prestito dopo unmap e
lo acquisisce nuovamente solo dopo il mapping successivo.

Il loader vieta segmenti nell'intera finestra di 64 KiB e nelle due
guardie. Il mapper controlla che guardie e coda inutilizzata restino
assenti. Una assegnazione inferiore al massimo non azzera byte nella
coda; i test controllano anche questa delimitazione del buffer.

Una allocazione unica evita rollback di assegnazioni multiple. Richieste
invalide, OOM, finestra occupata o backend assente falliscono prima della
pubblicazione. Se fallisce il controllo del frame o il pin dopo
allocazione viene tentato il rilascio; se fallisce la verifica dei byte
dopo la pubblicazione viene tentato unmap/rilascio. Questa gestione non
recupera da #PF hardware o corruzione concorrente dei metadati.

## Prove

- TDD: i test C sono stati aggiornati alla nuova API prima di implementarla;
  la compilazione iniziale ha respinto i conteggi aggiuntivi con la vecchia API.
- Host C: tutte le dimensioni 1-16, estremi e contenuto azzerato, OOM
  con solo 15 pagine disponibili, ID obsoleti, copie dell'owner, parent
  corrotti e corruzione dell'ultima PTE senza unmap parziale.
- 256 cicli di crescita/riduzione con budget ripristinato, stack estraneo
  preservato e invalidazioni senza indirizzi duplicati o mancanti.
- Preflight su ogni PTE occupata, incluse entrambe le guardie, senza
  allocazioni, PTE aggiuntive o avanzamento dell'identificatore.
- Rust: quattro test nuovi, ogni larghezza CPU 32-52, tutti i 4095 offset
  non allineati, overflow/conteggi errati e 8192 confronti con un modello u128.
  Passano anche i quattro test precedenti sulla politica delle regioni.
- Build completa del bootloader C/C++/Rust e conversione ELF valida
  con sette mutazioni invalide respinte.
- QEMU: 10/10 scenari diagnostici del loader, 4/4 scenari del passaggio
  al kernel e 7/7 scenari del kernel autonomo.
- Le prove nuove verificano NX sull'ultima pagina, lettura dopo unmap
  dell'ultima pagina e guardie inferiore/superiore: CR2 e codici #PF
  attesi, stack IST, CR3 e ExitBootServices riuscito.
- Trace QEMU `int,cpu_reset` presenti; nessun triple fault rilevato.
- Diff verificato e file della task normalizzati LF. I sorgenti centrali
  mapper/test hanno 201/205 righe; header e validatori restano compatti.

I log locali sono ignorati sotto build/. Gli scenari kernel finali sono
in kernel/build/firmware-bf7e82f7c2c54f6988c6a48f6c69c844; le protezioni
handoff sono in arch/x86/boot/uefi/build/kernel-test-a92ca9e60c624acea580dd5a33215e6e.
I dieci scenari diagnostici sono in firmware-test-6b2d5369c26944bfa07013d97ec7bc7f.
Le immagini ordinarie sono state ricostruite e installate dopo queste prove diagnostiche.

| Artefatto | SHA-256 |
| --- | --- |
| BOOTX64.EFI finale | 07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3 |
| kernel.elf finale/installato | 300695749E1C7859D44F4216A8192B782A3DD89BEB0F6ACDE889C46223A11031 |
| window-range-host.log | A814B23060197D0AF90EDDE355893906C7E97550FFC0431C2280BD5C77B4EFEE |
| window-range-final-firmware.log | 693E0D742DF2CF497155172937BC861D6656B93D14C7120B2142822B8212DB40 |
| window-range-final-build.log, boot | BED2D1D659DE4B68D92693F64AE04BB598B027FFBC33C73746E28CE35D8CCA39 |
| window-range-final-protection.log | 4B9BA31287C5CC3038000D8043E19819B0FFC8B23E7E1B5CFE4C71EB3A9E3E5E |

## Budget, limiti e prossima task

Il kernel ELF occupa ancora 24576 byte sul disco e 28 KiB residenti
arrotondati per segmento. La finestra usa fino a 64 KiB fisici mentre
attiva; il test li restituisce integralmente. Non alloca metadati
aggiuntivi o pagine di tabelle. L'owner fisico resta di 6448 byte.

Solo QEMU/EDK2 q35, CPU max TCG, un processore, interrupt disabilitati.
Mancano frame non contigui, finestre multiple, SMP e shootdown TLB,
reclamazione firmware, PAT personalizzato, cifratura e hardware reale.
Il protocollo di ownership opera fra chiamanti ring 0 fidati.

La prossima task puo costruire un piccolo allocatore di oggetti sopra
questo blocco, con allineamento, handle, limiti e test dei rilasci.
Il kernel ibrido resta una progettazione futura: il mapper da solo
non fornisce isolamento dei servizi o prestazioni gaming.

[API e comandi](../kernel/memory/page-window.md),
[Intel SDM Volume 3A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf),
sezione 4.10.4: invalidazioni dopo modifiche alle strutture paging.
