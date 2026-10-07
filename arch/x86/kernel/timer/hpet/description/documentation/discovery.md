# R-001: lookup HPET senza accessi fisici diretti

Aggiornamento: il successivo [collegamento al kernel](kernel.md) e completato
e testato sul firmware reale QEMU. Questa pagina conserva la prova precedente
del lookup isolato; i limiti kernel descritti sotto si riferiscono a quella fase.

Design scelto: watchdog indipendente e HLT. Questo blocco estende lo stesso
modulo description; mapping, comparator e IRQ sono ancora passi successivi.
La ricerca non e ancora chiamata dal kernel e non risolve l'attesa R-001.

## Contratto e decisioni

Il caller passa Entries/Count della directory ACPI gia validata e posseduta,
physical bits verificati e un reader con la forma di KERNEL_ACPI_READ.
Il reader copia l'intera richiesta oppure fallisce e decide quali regioni
ACPI siano accessibili. Nessun cast di un indirizzo fisico a puntatore.
Non si reimplementa il parsing RSDT/XSDT o il mapping del page window.

Pseudocodice di progetto, non codice eseguibile:

```text
validate owned immutable list, output separation and every signature span
for at most 256 addresses:
    copy four signature bytes through authorized reader
    if HPET:
        reject a second HPET signature
        validate full fixed extent; copy and decode local table
if exactly one validated description and scan complete: publish
otherwise return absent, invalid, read-failed or ambiguous; preserve output
```

Scansione O(n), massimo 256 copie da quattro byte e una da 56 byte.
Il limite 256 segue l'interfaccia corrente della directory del progetto;
non e un limite imposto dallo standard HPET. Piu tabelle HPET possono essere
lecite sulla piattaforma: il profilo v0 rifiuta la scelta ambigua senza
selezionare silenziosamente il primo device. Ripetere lo stesso indirizzo
HPET ha lo stesso risultato. Gli indirizzi non HPET possono ripetersi.

L'assenza e distinta dall'errore: una lettura fallita, anche dopo un candidato
valido, impedisce FOUND. Il reader deve restituire esattamente 1 per successo,
scrivere tutti i byte, non trattenere buffer locali e non modificare lista o
output. Context/output disgiunti sono un contratto del caller: la dimensione
del context opaco non e conoscibile dal lookup. Non e un'interfaccia utente.

[Intel HPET 1.0a, sezioni 3.2.4 e 3.2.6](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf)
descrive l'indirizzo assegnato dal firmware e il successivo controllo delle
capabilities hardware. Questo lookup esegue solo la prima parte su dati
copiati; la descrizione non prova ownership MMIO o capacita di generare IRQ.

## Threat model e risorse

| Rischio | Controllo | Prova |
|---|---|---|
| Range fisico fuori CPU/overflow | Rust checked_add, prima della copia | Matrice limiti 32..52 bit |
| Output sovrapposto alla lista | Bounds/allineamento prima di leggere entries | Test alias e lista non allineata |
| Falso successo dopo errore tardivo | Commit finale, output invariato su errore | Failure su ciascuna delle quattro letture |
| HPET duplicato/scelta arbitraria | Rifiuto del secondo candidato | Stesso indirizzo e due tabelle distinte |
| Firma diversa nella copia completa | Decoder riapprova firma/header/checksum | Mutazione con checksum valido |
| Loop/device reads illimitati | Count <= 256 e range numerico bounded | Limite 256 e rifiuto 257 |

Nessuna allocazione e nessuna risorsa device acquisita: nessun rollback di
mapping in questo blocco. ASan strumenta il C host, non Rust ne RAM fisica.
La funzione e rientrante con buffer disgiunti; richiede input immutabile e
reader non rientrante per la singola chiamata. Non risolve race DMA/firmware.

## Prove 2026-10-06

Lo script del modulo esegue tre test Rust, le due suite C/FFI per 100
ripetizioni con ASan e dieci avvii UEFI su QEMU q35/TCG/OVMF. La suite
discovery include assenza, profilo invalido, limite massimo, duplicati,
checksum errato, cambio firma, range valido per quattro byte ma non 56,
reader mancante, null output, alias e mancata pubblicazione sugli errori.

I test UEFI usano fixture: non leggono la vera directory ACPI QEMU e non
provano IRQ HPET, HLT o boot del kernel. Gate dell'integrazione kernel e
hardware restano aperti; nessuna coverage percentuale e stata misurata.

Esecuzione finale: tutti i test indicati passati. Evidenze in
`kernel/build/hpet-description-3bef06369df74dce8aafd5762ede0d2a/`:
tools.log, dieci serial/debug/stderr log e source-manifest.json SHA-256.
Lo script verifica che i sorgenti non cambino durante le prove.
Compilazione C11 /W4 /WX e Rust -D warnings; link UEFI senza runtime host.

Cppcheck 2.22.0, enable all/inconclusive/exhaustive, win64/C11, cinque C:
nessun finding error/warning/performance. Rimangono informazioni sugli header
standard non forniti e unusedFunction per efi_main, entry point del linker.
XML conservato nelle evidenze, senza sopprimere tali segnalazioni. I limiti
di righe, file diretti, whitespace e git diff --check sono verificati.
