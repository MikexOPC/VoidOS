# Checkpoint: ciclo kernel continuo e arresto verificato

Validazione del 5 ottobre 2026. Un solo blocco: durata del servizio timer
sul BSP, senza aggiungere scheduler, AP o nuove periferiche.

## Implementazione e revisione

Il ciclo arma un oneshot, dorme, consuma la scadenza fuori dall'IRQ e
riarma. Mantiene lo stesso token fino allo stop. Una decisione Rust
rifiuta richieste invalide o Count/Consumed discordanti e gestisce
la saturazione di Count. L'accumulo dei risvegli verifica overflow.
Il controllo statico occupa 48 byte; nessuna allocazione aggiuntiva.

Una richiesta kernel fidata o un debugger locale possono chiedere
arresto. L'evento in corso viene consumato prima del cleanup.
LOOP_CLOSED richiede owner chiuso, token vuoto, zero errori e LAPIC
quiescente, dopo il ripristino della configurazione precedente.

La revisione ha separato le transizioni degli eventi in runtime.c
(207 righe) dalla calibrazione/bootstrap in driver.c (231 righe).
Gli accessi registri restano nella stessa implementazione validata.
Header e backend compatti non sono stati allungati artificialmente.

## Prove

- Test host C: mille cicli arma/gestisci/consuma, ultima sequenza u32,
  alias e corruzioni, fallimenti di scrittura; aggiunti rifiuti del ciclo
  prima dell'inizializzazione. I mock non eseguono HLT in ring 0.
- 59 test Rust complessivi; incluso stop 0/1, richiesta invalida,
  evento non consumato e confine UINT32_MAX.
- Regressione ordinaria finale: 23/23 scenari QEMU; fotografie reali
  e 37 alterazioni TX, 16 del registro IRQ, 14 del timer rifiutate.
- Conversione immagine: immagine valida e sette mutazioni invalide
  rifiutate. Compilazione C/Assembly/Rust senza warning accettati.
- QEMU q35/TCG, 128 MB/1 CPU: 12 IRQ timer consumati, poi stop esterno.
- QEMU q35/TCG, 256 MB/4 CPU: 9 IRQ timer consumati, poi stop esterno.
  Gli AP restano inattivi; questo non dimostra supporto SMP.
- Entrambe le prove verificano RAM del controllo e owner, token vuoto,
  Phase Closed, Count=Consumed=consegne, IRQ reali dalla trace,
  RIP di ritorno nel sonno con IF=1, byte STI/HLT/CLI, IF terminale=0,
  HLT terminale, ISR/IRR vuoti, LVT/divisore ripristinati e Count hardware zero.

Log finali ciclo:
`kernel/build/loop-firmware-7991e17dc408449c9e031d1d5419fe44/`
e `kernel/build/loop-firmware-89179ff83bce421a8c08672b306fe0ce/`.
La richiesta debug modifica solo StopRequest dopo lettura e readback.
Le prime prove hanno corretto la query iniziale al debugger e
l'allineamento della fotografia RAM; non sono contate come successi.

Regressione finale: `kernel/build/firmware-7c9c568add4841ff9f54212b6eb7f435/`.
Entrambi gli ELF hanno 126976 byte. SHA256 ordinario:
`37A01C517C77E92F0228A0EE38C53123DADC25CCF426DA1BCA73FB41EFA25D21`.
SHA256 ciclo continuo:
`6B31C0FCF10DA12512B2814BEF0DDCD34E4189DC7986CAC6D6C8D65135EA6F53`.
La ESP ordinaria riceve l'ELF finito verificato; il ciclo continuo resta
disponibile in `kernel/build/event-loop/` e viene installato solo su scelta
esplicita della modalita. Gli artefatti generati restano fuori dal commit.

## Limiti e prossimo blocco

Il ciclo e opzionale (`-EventLoop`); il default conserva tre scadenze
e poi termina. KERNEL_READY arriva dopo lo stop, non durante il ciclo.
StopRequest non sveglia HLT: se il timer si arresta senza generare IRQ,
manca ancora un watchdog indipendente. Il budget di risvegli non e
un timeout in secondi. Nessuna garanzia di latenza o supporto hardware
fisico viene dedotta da QEMU. Prima dello scheduler occorre definire
e verificare questa politica di timeout.

Riferimenti ufficiali e comandi riproducibili sono nel
[documento del ciclo](../../kernel/timer/runtime.md).
