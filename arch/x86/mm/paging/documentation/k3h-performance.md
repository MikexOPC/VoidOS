# K3-H: verifica paging senza autorizzazioni memorizzate

Estensione a paging e controlli memoria autorizzata dall'utente il 30 ottobre 2026.
Obiettivo: risolvere la scadenza console di 600 secondi senza cambiare timeout,
ownership, permessi, bounds o politica di rollback. AP ancora fermi.

## Interventi equivalenti

L'inventario memoria controlla tutti i descrittori. Finché il prefisso è
ordinato e disgiunto, il confronto con il limite precedente dimostra l'assenza
di overlap con quel prefisso. Alla prima inversione passa definitivamente ai
confronti pairwise originali, includendo tutti i descrittori precedenti.
Il limite è calcolato solo dopo la validazione Rust di allineamento e somma,
prima dell'esclusione della pagina zero. Le mappe non ordinate restano ammesse.
Nessuno stato viene conservato fra invocazioni.

Il controllo degli alias copia al massimo 128 indirizzi delle tabelle in un
array locale, lo ordina e cerca ogni foglia con ricerca binaria. Non modifica
l'ordine topologico dell'inventario. Legge nuovamente tutte le entry foglia,
rifiuta viste assenti e ogni alias delle tabelle. Il buffer locale è separato
con noinline per non unirlo al grande draft del chiamante sullo stack.
Restano tutti i controlli per foglia, dispositivo, lease e permessi del walker.
Nessuna nuova istruzione privilegiata, accesso MMIO o maschera hardware.

Per n descrittori: percorso ordinato O(n), fallback non ordinato O(n²).
Per T tabelle e L foglie: ordinamento O(T²) con T ≤ 128, ricerca O(L log T)
in luogo di O(L T). Memoria aggiuntiva limitata a 1.024 byte sullo stack.

Un ulteriore confronto ripetuto è nell'allocatore fisico: dopo che ogni
extent libera e ogni lease precedente sono già state validate, i confronti
di overlap riapplicavano le stesse verifiche Rust a ciascuna coppia. La
correzione conserva la validazione Rust di tutti gli intervalli e confronta
in C i soli estremi già verificati. Non sposta la lettura fuori dal lock,
non riduce i controlli di Id/pin/accounting e non conserva risultati fra
chiamate. Il limite CPU ≤ 52 bit dimostra che le somme non fanno overflow.
Il riferimento originale viene conservato nei soli test; 7.903 confronti
differenziali su capacità, lease mutate, overflow, duplicati e pin passano.

## Test

Il riferimento memoria è la versione pairwise precedente, solo nei test.
Test differenziali deterministici: 3.063 casi su mappe ordinate/inverse,
permutazioni, duplicati, overlap con un prefisso lontano, adiacenze, pagina zero,
limite fisico 2^52, somme invalide e pubblicazione dell'output solo al successo.
Alias: 8.512 casi su tutte le capacità 1–128, ordine inverso dei frame,
ogni possibile frame esposto da una foglia, flags A/D/NX e viste assenti;
l'inventario originale deve restare invariato. Il modello conserva il vecchio
confronto lineare come riferimento. Questi test verificano equivalenza degli
algoritmi, non accessi hardware reali o assenza di ogni race del sistema.

Suite paging MSVC /W4 /WX, ASan, Rust debug/release e Clippy: PASS.
Dieci gruppi preesistenti ripetuti cento volte conservati. La fixture e i casi
dispositivo sono separati dal runner funzionale per eliminare il file da
500 righe senza rimuovere casi. Evidenze: kernel/build/k3h-paging-perf-host-v1/.
La suite finale v5 ripete tutti i test; evidenze in
kernel/build/k3h-paging-perf-host-v5/. Cppcheck: 29 information, quattro style,
zero error/warning/performance/portability. Stress fisico ASan con 2/4/8 thread:
2.000/4.000/8.000 cicli con budget e pin ripristinati; evidenze in
kernel/build/k3h-physical-host-v5/. Non è una prova di SMP in guest.
La console v4 passa una sessione completa entro la deadline invariata
(pointer-interactive-check-af96829a495e4c399039e1db4adbc1cc); la revisione v5
comprende anche la correzione allocatore ed è stata ricollaudata separatamente.
La campagna finale v5 passa 10 preparazioni, un rollback, 10 boot ordinari
e 10 sessioni console VirtIO/VT-d consecutive. Evidenza console:
kernel/build/pointer-interactive-check-0296a0ad972744f88b53e4aefe355a5f/results.json.
Timeout invariato; nessuna verifica disattivata. Osservazioni host fino al
puntatore circa 201–511 s, con polling diagnostico modificato dopo il quarto
boot: non è un benchmark comparabile, né una garanzia di boot rapido.
Lo startup resta costoso; non si attribuisce un guadagno percentuale non
misurato. AP ancora fermi, nessun nuovo CR3 attivato.
La review finale, dopo sole rinomine di variabili del modello di test,
ripete tutti i 19.478 differenziali ASan. Cppcheck finale in
kernel/build/k3h-paging-review-v5/: 29 information, due style preesistenti,
zero error/warning/performance/portability. Coverage linee/branch N/D;
Valgrind e hardware fisico non eseguiti.
