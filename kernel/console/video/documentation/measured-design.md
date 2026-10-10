# Disegno misurato nel profilo input — 10 ottobre 2026

Prosegue il [piano](active-design.md): un framebuffer pinned, owner privato,
profilo diagnostico reversibile. Nessuna estensione del RIP ammesso per IRQ.
Scelta operativa del test: una cella8x16 prima di ciascuna finestra Assembly
della task1, C a IF0; poi poll/yield/wait esistenti. Nessun disegno negli IRQ.

Un misuratore posseduto C associa flusher, callback clock e contesto stabili,
respinge copie/rientro e misura un solo flush con budget1. Policy numerica
Rust senza puntatori: mask24/32, differenza modulo contatore, zero/progresso,
soglia ceil(3.579545MHz/10)=357955 tick, circa100ms. Una misura oltre soglia
e terminale; la cella puo essere gia scritta/confermata, nessun rollback
pixel fittizio. Niente retry automatico dopo una violazione temporale.

La soglia e diagnostica, non un limite universale per tastiere o hardware.
Il primo obiettivo10ms (35796 tick) e fallito al secondo boot della prima
campagna:75935 tick, circa21.21ms. Non e dichiarato raggiunto. Rimane una
metrica con contatore SlowCells; il gate100ms coincide con il budget del
poll input esistente (PM_HZ/10), non prova efficienza o latenza realtime.
La misura riguarda il flush e le due letture clock, non tutto il dispatch o
le stampe seriali. Il delta modulo presuppone intervallo inferiore a un giro
del contatore; non riconosce uno stallo di uno o piu giri. Deadline host della
VM e confronto di input/progresso sono controlli distinti, non una soluzione
al guasto del clock. Nessuna garanzia hard real-time o hardware fisico.

## Fonte prima degli accessi

[ACPI6.6 §4.8.2.1 e §4.8.3.3, tabella4.14](https://uefi.org/specs/ACPI/6.6/04_ACPI_Hardware_Specification.html#power-management-timer-pm-tmr),
consultata il10 ottobre2026: PM_TMR e read-only, accesso32bit, TMR_VAL[23:0]
e E_TMR_VAL[31:24] se FADT TMR_VAL_EXT annuncia32bit; frequenza3.579545MHz,
valore iniziale non assunto. Usiamo solo x86_pm_read e il clock gia acquisito
dalla sessione: Port verificato1..65535, Ready, Mask24/32. Nessuna porta
inventata, nessuna scrittura o nuova istruzione privilegiata. Il gateway
esistente serializza la misura TSC e verifica store/readback UC;
[fonti del gateway](rendering.md). Rust valida scalar/limiti; C possiede
lease, griglia e callback; Assembly conserva la finestra IRQ esistente.

## Carico e accettazione

```text
worker IF0 -> modifica una cella RAM -> PM start -> flush1 -> PM end
 -> Rust classifica durata -> finestra IRQ Assembly gia verificata
 -> input/heartbeat -> wait o yield -> prossimo aggiornamento
stop -> cleanup input e stack -> report durata/perdite -> cleanup watchdog
```

La cella cicla A/è/€: font reale e UTF-8 completo nel gateway. Controlli
finali: numero celle, durata massima, clock mask, ultimo scalar, zero perdite
osservate nella coda, IRQ input e incremento degli ack HPET tra aggiornamenti.
Il watchdog viene armato dal suo idle esistente, non durante il flush IF0:
non puo interrompere uno stallo C a interrupt mascherati. Non cambiare questo
profilo per dichiarare una protezione NMI che non esiste.

Test: confini temporali/wrap/clock fermo Rust e C/ASan, copied owner/rientro,
fallimento draw e limite superato; immagine isolata TestConsoleDraw, non
installabile. Dieci boot finali con input italiano/shell seriale,32 tasti
aggiuntivi, confronto pixel fisici/visibili della cella, budget e cleanup.
Il test non collega ancora echo/output shell alla griglia.

## Inventario durante i tre stack del collaudo

Il primo boot ha respinto il disegno: l'inventario precedente alle task
contava904 foglie, il grafo corrente952. Una cattura diagnostica separata
ha confermato la differenza48; non viene adottata automaticamente.
Il backend scheduler verifica tutti gli otto slot: owner originali, stato
READY, generazione, pool/root, lease pinned16 pagine, guardie/PTE, assenza
di slot estranei, lease distinti. Restituisce solo il totale verificato.

Il profilo di test ammette esattamente48 pagine e il budget corrispondente.
Deriva sullo stack una copia dell'inventario, aumentando solo contatori RAM
e foglie; owner framebuffer resta originale. Presta questa fotografia per
una cella a IF0, poi ripristina il puntatore originario anche su fallimento.
La scansione completa resta obbligatoria: grafo, tabelle pinned, MMIO,
cache, guardie e alias sono confrontati dal renderer esistente. Nessuna
PTE modificata, nessuna cattura del grafo accettata a posteriori, nessuna
mutazione dell'inventario baseline. Il prestito non attraversa la finestra
IRQ e non puo essere usato fuori da questo profilo diagnostico.
