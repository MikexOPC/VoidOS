# Checkpoint: mapping simultanei del kernel

## Risultato e revisione

Il kernel ha quattro slot con handle distinti, fino a 128 KiB per slot,
RW/NX supervisor, guardie e coda assente. Acquire, data, release e detach
mantengono ownership fisica e virtuale esplicite. Rust no_std controlla
geometria, indirizzi degli handle e limiti fisici con soli scalari.
Il mapper usa il PT ereditato; non cambia CR3 o alloca nuove tabelle.

La revisione ha controllato limiti degli indici, alias dei metadati,
identita delle lease, ID monotoni, pin, overflow e stato delle 136 PTE.
Il frame viene verificato prima di pubblicare le PTE. La restituzione
fisica viene verificata prima dell'unmap; i frame restano bloccati fino
a tutte le invalidazioni. Nessun successo viene pubblicato prima del
controllo dei byte azzerati e delle invarianti finali.

Gli assert C vincolano il layout condiviso con Rust. L'owner occupa
160 byte, l'handle 32. Mapper C 204 righe, suite host 209 e kernel/main.c
278: moduli entro i limiti concordati, header e validatori compatti.
La build resta C11 `/W4 /WX`, MASM `/W3 /WX`, Rust `-D warnings`,
senza CRT, import, TLS o rilocazioni.

## Evidenze sul codice corrente

- Test host completi: `kernel/build/regions-host.log`.
- Quattro blocchi da 32 pagine simultanei: byte distinti, pin esclusivi,
  record esauriti, rilascio selettivo e riuso azzerato senza alterare vicini.
- Backend assente: otto invalidazioni per map/unmap di quattro pagine,
  nessun handle pubblicato e budget ripristinato.
- OOM fisico con slot libero: nessuna PTE, invalidazione o consumo di ID.
- 8640 alterazioni non A/D delle PTE respinte prima dell'unmap;
  controlli dei flag e limiti fisici dei genitori.
- 128 cicli con allineamenti diversi, handle modificati, copie obsolete,
  alias, owner copiato, detach vietato con lease vive e reinitialize sicuro.
- 23 test Rust totali, inclusi geometria e limiti fisici del nuovo mapper.
- Convertitore: immagine valida e sette mutazioni invalide verificate.
- 22/22 scenari QEMU, nessun triple fault, dopo la revisione finale.
- Ogni scenario legge le 136 PTE reali, confrontando frame, RW/NX,
  guardie e code. I blocchi persistenti sono 32 e 16 pagine, 192 KiB.
- Quattro nuovi scenari: unmap seguito da accesso, guardia bassa, guardia
  alta e NX. Verificati #PF, error code, CR2, RIP NX, frame IST e arresto.
- Regressioni: 128/256 MiB, panic, finestra temporanea, stack autonomo,
  vecchio stack revocato, NMI, double fault e fault annidato.

Log finali: `kernel/build/firmware-2575bdb72bc946c1bdc9628a75ec18d2/`.
ELF costruito, installato e copiato nel caso ready:
`3565FC6D3FD4185E55AC5C4038A72A70FD202063032DD4026BF61EAFD4DAAD5A`.
Disco 57344 byte, segmenti residenti arrotondati 61440 byte: una pagina
in piu rispetto al checkpoint delle allocazioni allineate.
EFI resta invariato, SHA-256
`07D09D2272CA2D3B31CBCB10276435FCE124554336159132C0C681AF6C3F9DF3`.

## Limiti e prossimo blocco

E un mapper bounded di bootstrap UP/IF=0, non il memory manager finale.
Non gestisce concorrenza, SMP, mapping MMIO, enorme spazio virtuale,
permessi dinamici o nuove tabelle. Il prestito C dei byte richiede che
il chiamante scarti i puntatori prima del rilascio; gli handle non
impediscono di usare una copia di un vecchio puntatore.

Tabelle paging, mappa e altre risorse loader restano riservate.
Un errore terminale arresta il kernel; corruzioni inattese conservano
il pin senza tentare di liberare frame con mapping non verificati.
Host e QEMU non dimostrano compatibilita con hardware fisico.

Il prossimo blocco puo usare questi mapping per strutture persistenti
del kernel. L'estensione delle tabelle e la reclamazione del loader
richiedono prima un inventario preciso delle risorse paging ancora vive.
Timer e scheduler restano passi distinti, con verifica dei prerequisiti.

[API e lifecycle](../../kernel/memory/region-mappings.md).
