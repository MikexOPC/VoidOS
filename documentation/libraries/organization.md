# Librerie condivise del progetto

La posizione dipende dalle dipendenze del codice e dai suoi utilizzatori.
Ogni funzione pubblica ha un nome che descrive il compito e parametri
espliciti. Le librerie attuali non allocano memoria e non usano uno stato
globale modificabile.

| Cartella | Responsabilita |
| --- | --- |
| `lib/` | Funzioni generali utilizzabili da boot, kernel e altre architetture |
| `arch/lib/` | Supporto comune ai bootloader e alle architetture, quando serve |
| `arch/x86/lib/` | Funzioni che dipendono da x86 |
| `kernel/lib/` | Supporto che dipende dall'ambiente del kernel, quando esistera |
| `arch/x86/boot/uefi/` | Integrazione UEFI, proprietari delle risorse e sequenza di boot |

`arch/lib/` e predisposta per il futuro; oggi non contiene moduli.
Una funzione generale usata da due bootloader resta in `lib/` se e utile
anche al resto dell'OS. Un componente comune alla procedura di boot o al
supporto delle architetture puo andare in `arch/lib/`. Le primitive
generali non dipendono da `arch/` o da servizi firmware.

## Componenti gia riutilizzati

`lib/memory/address_ranges.*` contiene il confronto numerico degli
intervalli. ELF, mappa UEFI e piano di paging usano la stessa funzione.
I test propri della libreria sono in `lib/memory/test/`.
`lib/memory/buffer_capacity.*` centralizza margine, allineamento e limiti
numerici dei buffer con un'implementazione Rust senza puntatori o heap.

`arch/x86/lib/processor/` contiene l'accesso CPUID, la lettura dei registri
e i controlli numerici Rust del profilo di paging a quattro livelli.
I file `cpu/` del loader mantengono l'adattamento agli status EFI, la
validazione degli argomenti di boot e la pubblicazione dei risultati.

Le interfacce comuni usano tipi interi standard e linkage C, utilizzabile
anche da C++. I controlli Rust sono `no_std`, senza heap, e hanno simboli
con prefisso `x86_`. Non dipendono dai tipi definiti in `uefi.h`.

## Compilazione e test

La build UEFI genera `os_memory.lib` e `x86_processor.lib` nella propria
cartella `build/`, poi li collega staticamente. La build di un futuro
kernel compilera le stesse sorgenti con i propri flag e artefatti.
Non si riutilizzano automaticamente gli oggetti del bootloader.

I test del loader restano nel suo `test/`. Comprendono i test Rust dei
controlli x86 e un consumatore C++ che include soltanto le interfacce
condivise. La DLL host estrae dall'archivio x86 soltanto i moduli numerici;
le letture privilegiate reali vengono verificate nel percorso QEMU.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```
