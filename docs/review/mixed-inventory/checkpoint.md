# Checkpoint: inventario misto e LAPIC mantenuto a KERNEL_READY

La prova COM1 conserva il mapping LAPIC dopo tre consegne hardware.
Il paging distingue ora foglie RAM e MMIO, con una sola pagina dispositivo
autorizzata tramite owner live. UART e rotta vengono chiuse; IF resta zero.
Questo prepara l'estensione del driver senza introdurre IRQ permanenti.

## Implementazione e review

- Percorso ordinario ancora restrittivo: nessuna autorizzazione UC implicita.
- Footer di 48 byte: contatori RAM/MMIO e record virtuale/fisico/cache/owner.
  Inventario totale 3144 byte, con prefisso precedente invariato.
- Classificazione Rust scalare, senza unsafe, con indirizzi ricostruiti
  dalla vista PT; rifiuto di alias fisici della pagina LAPIC in altri slot.
- Autorita verificata prima e dopo la scansione: owner, PTE, guardie,
  CR3, CPUID, APIC_BASE, PAT e stato cache. Il puntatore memorizzato
  nel record non viene usato per dereferenziare metadati non verificati.
- Riserve firmware e allocator disgiunti dal dispositivo; nessuna nuova
  allocazione di frame per il mapping. Pubblicazione solo dopo successo.
- Prima di mantenere il mapping, la chiusura verifica anche ISR/IRR vuoti.
  I percorsi di errore revocano solo dopo chiusura verificata della sorgente
  e della rotta; PTE estranee o interrupt pendenti impediscono la revoca.
- Il primo script QEMU cercava un campo CPUID non presente nei log:
  corretto usando lo snapshot APIC in memoria, poi ripetuta la campagna.

## Evidenza della working tree

- Suite host kernel passata; 46 test Rust complessivi.
- 25152 mutazioni dei metadati ordinari; 384 mutazioni del footer misto;
  64 mutazioni della PTE LAPIC, tollerando solo Accessed/Dirty.
  Prove di UC non autorizzata, alias WB, riserve incompatibili, owner/output
  sovrapposti e fallimento dell'autorita dopo scansione senza pubblicazione.
- Modello APIC con modifica del PAT selezionato: autorita rifiutata.
- Immagine valida e sette mutazioni invalide verificate.
- 23/23 scenari QEMU finali: kernel/build/firmware-f86349bda48a47a39509de8f68abfba6.
  Monitor indipendente per owner, PTE, guardie e footer; tre IRQ 0x40.
  KERNEL_READY: 118 foglie RAM, una MMIO, totale 119.
  Copie dei log con footer corrotto e PTE user rifiutate dal verificatore.
- SHA256 ELF: 5DDC38BDDA89C301EE8D92B07B47A76FD946BFDCD244934611F38073F2ABAAD6.
  ELF 106496 byte, segmenti residenti arrotondati a pagina 118784 byte:
  crescita di una pagina residente rispetto al checkpoint precedente.
  Hash identico per build, ESP installata e immagine ready-128 provata.

Le modifiche utente a diagnostici, header ACPI, main e contratto sono
preservate fuori dal commit. L'hash identifica la working tree testata.
Hardware fisico non testato; BSP operativo anche nella configurazione
QEMU con quattro CPU. Nessun supporto SMP o shootdown TLB attestato.

## Prossimo blocco

Trasferimento TX reale COM1, con buffer bounded, ownership esplicita e
budget dell'handler. RX, IRQ permanenti e scheduler richiedono passi
successivi. Il contratto misto attuale riguarda solo LAPIC.

[Contratto e fonte Intel](../../mm/paging/mixed-inventory.md).
