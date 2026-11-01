# K3-I — panic sul gruppo completo

Collaudo del backend NMI diretto già approvato. Nessun nuovo protocollo
del kernel: il gruppo diagnostico inizializza quattro core e li parcheggia,
poi un innesco presente solo nel test avvia il panic esistente.

## Casi

- BSP: chiamata volontaria, owner 0, targets=ACK=14.
- AP: il BSP invia un NMI fisico diretto al solo AP 3, dopo PARKED e IRET.
  L'handler reale su AP 3 elegge il reporter: owner 3, targets=ACK=7.
  È un panic originato da eccezione NMI su AP, non una chiamata volontaria
  da una task AP. Nessuna elezione o conferma viene simulata dal test.

Il BSP verifica gruppo, provider, root, mapping LAPIC trattenuto e maschera
online=15. Attende ICR libero con clock PM e polling limitati, emette una
sola IPI e non usa il logger dopo l'invio. Rimane in attesa attiva limitata
di un secondo: se il NMI di arresto non arriva, un panic volontario con
ragione diagnostica distinta rende il test fallito, senza attesa infinita.

Gli handler continuano a usare registri salvati in Assembly, record privati,
coordinatore Rust, deadline e seriale d'emergenza esistenti. Nessuna pagina
viene rilasciata; IDT finale condivisa, GDT/TSS/IST privati per core.

## Fonti e perimetro

[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§11.6.1, figura 11-12, ICR low +300h e high +310h, pending bit12,
destinazione fisica high[31:24], NMI low=4400h (mode100b, assert14,
edge e nessuno shorthand). §6.7.1: NMI bloccati fino a IRET; il gruppo
ha già completato quel ritorno prima dell'innesco.

Modulo per-CPU/panic e soli adattatori probe, build e test necessari.
Il backend di produzione e lo scheduler non cambiano salvo bug dimostrati.
Nessuna nuova coda, flag di risveglio condiviso o modifica del trampoline.

## Accettazione

Dieci boot consecutivi per origine, quattro core realmente ammessi,
un reporter, tre ACK, missing=outcome=0 e quattro record completi.
Verificare registri reali di tutti i core, stack/IST, root, descrittori,
frame normalizzati, pinning e budget. Solo il panic intenzionale è ammesso
nei FAIL; scaricare la seriale prima di fermare la VM posseduta.
Regressione sequenziale senza panic, suite host C/ASan e Rust, analisi
statica e audit degli oggetti d'emergenza. Hardware fisico e stress con
cause simultanee restano distinti da questi due casi deterministici.
