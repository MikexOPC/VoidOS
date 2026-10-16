# Preparazione AUX sul controller condiviso

Blocco dell'11 ottobre 2026. Il driver completo resta aperto: questo blocco
verifica la preparazione mouse, non movimento, task o IRQ12.
Decisione architetturale: [design concordato](design.md), tre byte e tre pulsanti.

## Contratto implementato

Un solo `KERNEL_PS2_CONTROLLER` contiene callback, mode e stato del canale
AUX. `kernel_ps2_mouse_prepare` richiede PREPARED, zero errori, mode30/34,
entrambe le interfacce ferme e IRQ disabilitati. Prestito esclusivo BSP/IF0.
Nessun comando viene ammesso mentre la tastiera è in streaming.

La fase diventa DIRTY prima del primo accesso; solo la sequenza interamente
verificata ripubblica PREPARED con `Auxiliary=1`. Close azzera quel flag
soltanto dopo conferma del mode e drain. Nessuna allocazione nuova.

`kernel_ps2_receive_channel` controlla lo status associato al byte letto;
la tastiera usa il wrapper con canale0, il mouse canale1. Canale inatteso,
timeout/parità o callback fallita sono errori, non byte da decodificare.
I comandi restano serializzati, senza handler concorrenti in questa fase.

## Fonti e sequenza hardware

[QEMU v10.1.0 pckbd.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c),
KBD_CCMD/KBD_STAT, `kbd_write_command`, `kbd_write_data`: A9 verifica
l'interfaccia e produce risposta controller00; A8 abilita AUX, A7 lo
disabilita; D4 instrada il prossimo byte a porta60. Status bit5 distingue
AUX, bit0 segnala dati disponibili, bit1 impedisce nuove scritture.
Mode finale30/34: bit4/5 disabilitano le interfacce; IRQ bit0/1 e
traduzione bit6 restano zero, si conserva solo il system bit2.

[QEMU v10.1.0 ps2.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c),
`ps2_write_mouse`: FF reset restituisce FA/AA/00, F5 disabilita reporting,
E6 seleziona scala1:1, EA stream, F2 restituisce FA e ID00. F4 non viene
inviato: il reporting resta fermo. Il controller viene nuovamente fermato,
drenato e verificato tramite readback. ID estesi non sono accettati.

Il binario QEMU installato è11.1.0, distinto dalla versione della fonte.
Il round trip positivo verifica questo contratto nel binario installato.
RESEND FE è un caso sintetico host: massimo tre invii, ciascuno con D4;
non viene dichiarato un comportamento osservato nel mouse emulato.

## Errori e rollback

Assenza di ACK, BAT/ID errati, errore dell'interfaccia e canale sbagliato
tentano close del controller intero, inizialmente fermo. Una risposta A9
nonzero non viene spacciata per discovery affidabile di mouse assente.
Se close fallisce restano DIRTY e CLEANUP_FAILED; nessun falso PREPARED.
Il chiamante non può avviare la tastiera su questo owner fallito: servono
cleanup confermato e nuova inizializzazione. Il fallback opzionale con
tastiera operativa dovrà essere verificato durante l'integrazione runtime.

Deadline esistenti per singola attesa PM, non durata totale della sequenza;
limite di poll per clock fermo, drain32, tre invii per comando.
Nessun accesso a output port/A20/reset CPU, MMIO, DMA o nuove pagine.

## Prove host finali

Log `kernel/build/mouse-controller-ps2-reviewed.log`, exit0;
output `kernel/build/ps2-decoder-441ed3606d844396a4a7c867f83bc3b7/`.
Hash in `mouse/sources.json`, tool output in `mouse/tools.log`.

- 87.454 controlli del trasporto;1.000 cicli completi prepare tastiera,
  prepare mouse, start tastiera, stop/close sul medesimo owner.
- Ordine dei cinque comandi mouse verificato da un modello indipendente;
  RESEND zero/due, doppia prepare respinta senza I/O.
- Failure injection a ogni callback della preparazione positiva; cleanup
  e retry verificati, senza pubblicare Auxiliary su un fallimento.
-12 scenari: canale errato, BAT, ID reset/query, ACK, silenzio, RESEND
  eccessivi, interface/readback, parità/timeout e clock fermo con IBF pieno.
- Owner invalidi respinti prima dell'I/O. Decoder esaustivo e7 casi Rust
  debug/release, ABI C, tastiera/editor/shell/route/discovery passano.
- MSVC `/W4 /WX`, ASan sui sorgenti C, Clippy e warnings Rust come errori;
  cppcheck senza error/warning/performance/portability bloccanti. Messaggi
  style/information non equivalgono a vulnerabilità dimostrate né zero messaggi.

## Prove firmware

Dieci boot consecutivi del profilo `-TestPs2Controller` passano.
Immagine: `kernel/build/ps2-decoder-firmware-278f234e36b64e57a42220af3c589845/image/kernel.elf`.
SHA256: `6913EBA82A702999D09CBA26D06AF14F6C2E54E4D629A01E08909D87EFBEB8F0`.
Risultati/hash: `results.json` nella stessa cartella; boot in
`kernel/build/scheduler-firmware-a7a98b8bfa594e349e3cd96fd778dab2/`.

Ogni avvio deve raggiungere `KERNEL_PS2_MOUSE_PREPARED`,
`KERNEL_PS2_CONTROLLER_SET2_OK` e `KERNEL_READY`, senza fault inattesi.
Il probe emette successo solo dopo close, flag AUX azzerato, finestra
idle valida e conteggio pagine libere uguale al valore catturato prima.
Non è una misura di leak di tutte le risorse del kernel.

Regressione tastiera/console: altri dieci boot consecutivi `-ConsoleShell`
passano su `kernel/build/mouse-controller-console-final/kernel.elf`, SHA256
`88778E196D6071B9C5AC631457A709EF10C71E2969D754EBAC830364E5E9B585`.
Evidenze `kernel/build/ps2-input-firmware-3992e13b60714bc0aa5994f0142b0b11/`:
98 key event e10 comandi shell per boot; budget20.419 prima/dopo;
768 pixel fisici e786.432 pixel visibili verificati per boot.
Questa immagine non abilita il mouse: è una regressione del percorso
tastiera e del nuovo layout owner, non una prova di input simultaneo.

Totale20 boot finali di due profili distinti; zero crash inattesi osservati.

## Review e limiti

Il blocco di compilazione PS/2 è stato estratto nel proprio script:
`kernel/scripts/build.ps1` scende da414 a396 righe, senza accorpare moduli
estranei. L'helper mantiene ordine e opzioni dei precedenti profili.
Una collisione di basename nella prima prova host è stata corretta
rinominando il nuovo sorgente mouse `protocol.c`; il log fallito è conservato.
Verifica strutturale finale:58 sorgenti della pipeline PS/2, LF, dimensioni,
massimo cinque file funzionali diretti e link locali; diff senza whitespace
errata. Nuovo test controller238 righe, sorgente protocollo74 righe.

APPROVE per preparazione AUX nel profilo QEMU. Non READY per il driver
completo: manca ricezione concorrente tastiera/mouse, route IRQ12,
coda/evento, task e stop dello streaming mouse. Il decoder non viene
esercitato dal probe firmware di questo blocco.
Valgrind non eseguito; ASan non strumenta Rust o RAM fisica.
Coverage linee/branch N/D; nessuna certificazione hardware reale o SMP.
