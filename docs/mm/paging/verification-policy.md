# Politica di verifica del paging (G5)

25 ottobre 2026. Decisioni approvate: V1-A audit in inattività (con interfaccia pronta per un audit
incrementale), V2 chiave economica a contatori di generazione, V3 contabilità per detentori (solo il
meccanismo), V4 regola scritta.

## Regola (V4)

1. **Chi costruisce verifica.** Un modulo che crea o modifica una mappatura ne controlla le
   invarianti in quel momento: tabelle proprie, foglie, permessi, nessun alias, lease pinnati. È
   lo schema già seguito da arena DMA, superficie, heap e dominio VT-d.
2. **Nessuna scansione dell'intero paging nei percorsi frequenti** (flush, disegno, input). Questi
   percorsi controllano solo owner, stato della CPU (CR3, PAT, bit fisici) e i contatori di
   generazione.
3. **La scansione completa è compito dell'audit.** Gira quando la console è inattiva, al massimo
   una volta al secondo. Se fallisce, la sessione si ferma (fail-closed).
4. **Le modifiche passano dalle API.** Ogni API di paging avanza la generazione (`invlpg`,
   ricarica di CR3); ogni operazione dell'allocatore fisico avanza `Generation`. Una scrittura che
   scavalca le API viene scoperta dall'audit entro il suo periodo: è il prezzo accettato (già H1-A).
5. **Pagine tenute per tutta una sessione:** si dichiarano nel registro dei detentori
   (`kernel/mm/physical/holdings.h`). I controlli di budget sottraggono il totale dichiarato invece
   di pretendere `FreePages` costante.

## Implementazione

| Parte | Dove |
|---|---|
| Chiave economica del percorso pixel: generazione del paging, generazione dell'allocatore, owner, CPU; scansione solo dopo un reset o un cambio di owner o CPU | `framebuffer/pixels/write.c` (`verify_private`) |
| Contatore `Generation` dell'allocatore (allocate, release, pin) | `kernel/mm/physical/allocator.c` |
| Audit con l'inventario atteso del live group (stack dei task) e la stessa contabilità | `framebuffer/live/group.c` (`x86_fb_live_audit`) |
| Audit in inattività, al massimo uno al secondo, con contatori nel report di chiusura | `framebuffer/console/runtime.c` (`x86_console_audit`), chiamato dal worker PS/2 quando non c'è nulla da disegnare |
| Registro dei detentori; la sessione GPU dichiara le sue pagine | `kernel/mm/physical/holdings.c`, `virtio/iommu/session.c` |
| Prova di manomissione: una PTE dell'heap modificata tramite la vista ricorsiva, senza API | `-TestPagingTamper`, `test/kernel/mm/heap/tamper.py`, campagna `paging-tamper` |

## Prove e misure

| Prova | Esito | Evidenza (`kernel/build/`) |
|---|---|---|
| Test host: chiave del percorso pixel (scansione al reset, generazioni accettate, audit che rifiuta snapshot corrotto e scrittura che scavalca le API), audit e detentori nel live group (587 controlli) | PASS | suite `x86-framebuffer`, `console` |
| Suite host complete | 33/34 (virtio-discovery: `read.c` dell'utente) | `test-run-034827e4d4a44d76b4b6cf324fdff0ca` |
| Manomissione di una PTE dell'heap, scoperta dal primo audit successivo | 3/3 | `paging-tamper-3bdb5dbf5938471ebca12b7a293bfe64` |
| Smoke set, compresa `paging-tamper` | 12/12 | `qemu-run-0d544c2ec18b421587fa81afe89a048b` |
| V2c, 10 boot, audit eseguiti e nessuna perdita PS/2 | 10/10 | `pointer-interactive-check-859f1480ecef47f294a0f8e628327a5a` |

Flush medio V2c, 10 boot per misura:

| | Flush medio (tick PM) | Scansioni complete nel flush |
|---|---|---|
| Prima dell'heap (G2) | 8.330 | 417 |
| Con l'heap, prima di G5 (G3) | 9.680 | 410 |
| **Con G5** | **7.544** | **10**, cioè una per boot, quella all'apertura dopo il reset |

- Le altre 277 variazioni di generazione sono state accettate senza scansione.
- Audit: 40 in 10 boot. Durata media 52.285 tick (circa 14,6 ms), massima 89.649 (circa 25 ms). Il
  worker resta bloccato per quella durata quando è inattivo; nelle prove non c'è stata nessuna
  perdita PS/2. Ridurre questo blocco è il compito dell'audit incrementale (V1-C).

## Limiti dichiarati

- L'audit gira solo quando il worker della console si sveglia, cioè con input o disegno in corso.
  Su un sistema del tutto fermo una manomissione resta nascosta fino alla prossima attività.
- Non esiste ancora un audit incrementale (V1-C): l'audit completo blocca il worker per la sua
  durata (vedi il report con le misure).
- La crescita dell'heap usa il registro dei detentori ma non è implementata (FW-013).
