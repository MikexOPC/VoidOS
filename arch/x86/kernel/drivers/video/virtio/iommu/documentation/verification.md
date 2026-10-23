# Verifica dell'inventario condizionata alla generazione (H1-A)

24 ottobre 2026. Decisioni approvate: H1-A verifica completa solo quando serve, H2 tutte le API di
modifica del paging avanzano la generazione, H3 rete periodica ogni 64 sessioni pixel, H4 solo il
percorso pixel della console.

## Meccanismo

- **Generazione del paging** (`x86_paging_generation_counter`, `arch/x86/mm/paging/inventory.c`):
  avanza a ogni `invlpg` (`x86_invalidate_page`, unico punto di invalidazione del kernel, in
  `io.asm`) e a ogni ricarica di CR3 (`x86_fb_flush`, unico punto di pubblicazione dei PML4).
  Ogni modifica delle tabelle fatta tramite le API passa da uno dei due.
- **Cache della verifica** (`framebuffer/pixels/write.c`): una sessione pixel ripete la scansione
  completa solo se è cambiato qualcosa tra generazione, owner, snapshot dell'inventario, stato
  dell'allocatore, root/PAT/bit fisici della CPU; oppure dopo 64 sessioni saltate; oppure dopo un
  reset (apertura della console). Allocatore e CPU sono entrati nella chiave durante i test: senza,
  lo sblocco di un pin o un cambio di PAT venivano accettati dalla cache.
- **Prezzo accettato**: una scrittura sulle tabelle che non passa dalle API viene scoperta dalla
  successiva scansione completa, al più dopo 64 sessioni. Il test host `pixel_generation` lo prova
  esplicitamente; i test di corruzione esistenti forzano la scansione completa e confermano che la
  scansione la rifiuta ancora.

## Risultati: tick PM per flush, mediana su 5 boot (immagini di profilo)

| Fase | A prima | A dopo | B prima | B dopo | C prima | C dopo |
|---|---|---|---|---|---|---|
| Flush | 16.307 | 2.517 | 37.224 | 7.059 | 43.653 | 8.662 |
| Verifica inventario | 11.133 | 389 | 27.155 | 949 | 29.278 | 1.155 |
| Present | — | — | 7 | 4 | 4.266 | 1.004 |
| Polling fault | — | — | 1.863 | 198 | 588 | 212 |
| Prepare | 407 | 200 | 1.459 | 1.059 | 1.506 | 1.063 |
| Resto del gruppo (disegno) | 4.009 | 1.583 | 4.882 | 3.394 | 5.624 | 3.831 |

V2c senza profilo (10 boot): flush medio 8.636 tick (era 40.832), present medio 983, polling medio
152, sezione IF=0 dell'arresto 237.962. Scansioni complete per boot: 39 su circa 990 sessioni
(15 periodiche, il resto dopo cambi reali di generazione).

Evidenze: A `pointer-interactive-check-b08ff43e062e4e148b153b04b0608de6`,
B `...-f1a4530c35d64564876b1079da60340d`, C `...-f564f157714a46458da1c7f0f52a27fe`,
V2c ×10 `...-567333b1c8ca4da7b07b05d0f219349a`, GOP ×2 `...-8b6870896e584815b9247e58d0fd7639`,
fallback ×1 passato; tutte in `kernel/build/`.

## Lettura onesta dei numeri

- La verifica scende di circa 25-28 volte, come atteso: è la causa trovata dal profilo.
- **Scendono però anche fasi che la modifica non tocca** (present da 4.266 a 1.004, disegno
  da 5.624 a 3.831, anche nell'immagine GOP). In QEMU TCG senza `icount` il clock virtuale, quindi
  il timer PM, segue il tempo reale dell'host: le misure includono la velocità dell'emulazione.
  Togliere decine di migliaia di letture di tabelle per flush alleggerisce l'emulatore e accelera
  tutto. Il guadagno relativo GOP contro V2c resta significativo; i valori assoluti no.
- V2c costa ancora circa 3,4 volte il GOP per flush (8.662 contro 2.517): present, prepare e il
  disegno nella superficie restano da analizzare se serviranno.
