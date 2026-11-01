# K1, tappa 5b — validazione del 27 ottobre 2026

Perimetro: revoca dell'identità del loader e soli adattatori di descrittori,
startup, build e test. [Contratto e fonti](design.md).
Nessun commit o push effettuato in questo blocco.

## Risultati acquisiti

| Verifica | Esito | Evidenza in `kernel/build/` |
|---|---|---|
| Policy/transazione host, ASan e cppcheck | PASS; 22 casi CPU, voci basse 1–255, ownership/errori, 1.000 cicli di successo/rientro; commit simulato | `test-run-4307a6fabf494e818026745efb1563a9/paging-retirement.log` |
| Suite host generali | 37 suite coperte e passate, tramite 36 successi iniziali e una riesecuzione mirata | `k1-retirement-host-summary.json` |
| Dieci boot finali dedicati | 10/10, lettura fisica delle 512 voci PML4, metà bassa vuota, GDT/IDT/TR alti, CR4 compatibile | `paging-retirement-e65002bc3fed4484aadd2769ba3c4a94/results.json` |
| Capsula scaldata prima della revoca | #PF atteso dopo MOV CR3; vector 14, error 0, CR2 uguale alla vecchia capsula | `paging-retirement-48f44674a107448a87b1be3195be0215/results.json` |
| Regressione kernel | 23/23 scenari, incluse guardie, NX, NMI e double fault | `k1-retirement-kernel-boot-final.log`, `firmware-d80c6715c90648b29944d018775bee5c/` |
| Campagne brevi | 12/12: heap, tamper, fallback, stall/recovery, console ordinaria, assenza IOMMU/video, disegno, shell, retirement e fault | `qemu-run-f2ae3f5bd7f14af28ae7a1ac41ac3586/results.json` |
| Console VirtIO con VT-d, dieci boot | 10/10, exit code 0, traduzione attiva, zero fallback, pixel/movimento/cleanup e budget ripristinato per ogni boot | `qemu-run-0e1e80c861594423ab6e9e7e662e7e17/results.json`, `pointer-interactive-check-3fcfa151a2b540c6a8cddb836504a66f/results.json` |

L'immagine del test dedicato finale ha SHA-256
`D1941FBFEA7C0E59D2D6C436EAC710492A1CCEBEF96C075CAD2D4485817A8C19`;
il loader ha SHA-256
`72CDB10E5BB594272E9B103E24D6F25CA580DC1AC0C5784E23EA93693D998C86`.
Gli altri profili conservano i propri hash nei rispettivi report.

La successiva modifica di sola formattazione segue `test.txt`. Sono stati
rieseguiti ASan/cppcheck (`paging-retirement-66e21cac35464ea8a1af7a34e94cb1d9/`)
e compilati nuovamente i profili bootstrap e VirtIO. Il confronto completo
degli ELF dimostra che codice eseguibile, layout e dati sono identici,
salvo il campo `TimeDateStamp` della directory debug PE conservata nella
sezione RO: tre byte diversi nel bootstrap, due nel profilo VirtIO.
Non viene dichiarata identità dell'hash dell'intero file.
`k1-retirement-style-equivalence.json` registra hash, offset derivati dalle
directory PE e confronto byte per byte; nessun artefatto è stato alterato
per normalizzare i timestamp. [Formato Microsoft, Debug Directory](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#debug-directory-image-only).

Il primo run host generale conserva il fallimento di formato in
`test-run-0ead3be6e1704345b18c1b637df4470f/results.json`. Su autorizzazione
dell'utente sono stati tolti soltanto due spazi finali alle righe 19 e 25 di
`kernel/drivers/video/virtio/discovery/configuration/read.c`.
La suite passa in `test-run-c291f8719b6941bb84485d4579e421a5/results.json`.
Non è stato riscritto il precedente report fallito né dichiarato un unico
run generale 37/37 che non è stato eseguito.

## Memoria, CPU e failure

- Nessuna allocazione o liberazione nel nuovo modulo. I test host sono
  strumentati con MSVC ASan e warning come errori; non eseguono istruzioni
  privilegiate. Usano la matematica Rust esistente, con backend hardware
  e inventario simulati.
- I boot reali QEMU verificano la parte Assembly, i descrittori effettivi,
  l'assenza delle traduzioni basse e il fault dopo il riscaldamento della
  vecchia traduzione. Il codice controlla budget invariato, generazione
  avanzata di uno, voci alte invariate salvo A/D e inventario ridotto delle
  sole tabelle/foglie rimosse.
- Un fallimento pre-commit non modifica la root. Un fallimento post-commit
  è terminale; le prove host verificano che il ramo non venga reinserito.
- Le pagine e le tabelle del loader restano riservate: non è implementato
  il loro recupero. ASan non dimostra assenza di leak dell'intero kernel.
  Valgrind non è stato eseguito sul kernel freestanding Windows.
- Il profilo è BSP, IF=0, quattro livelli, PGE/PCIDE/LA57 disattivati.
  Non introduce SMP, user mode, SMEP/SMAP o KPTI.

## Infrastruttura e limiti delle prove

Alcuni wrapper PowerShell iniziali sono stati interrotti dopo che i log
avevano già riportato il completamento dei test. Non sono usati come
dimostrazione di un exit code positivo. Il runner delle campagne ora apre
il handle del processo e attende il processo di test direttamente, anziché
usare `Start-Process -Wait` sull'intero albero; il profilo fault è stato
rieseguito attraverso questo percorso con exit code 0.
La campagna VirtIO interrotta nella sessione precedente non conta come PASS.

Nuovo modulo: quattro sorgenti diretti, massimo 186 righe; controllati
limiti, spazi finali, diff e link documentali. Non è una certificazione
globale di tutti i sorgenti storici del repository.
Coverage di linee/branch non misurata. Nessuna prova su hardware fisico.
K1 è chiuso per il profilo QEMU descritto; nessuna avanzata a K2 in questo
blocco. La serie VirtIO finale contiene 11.495 presentazioni complessive,
con `Translated=true`, `EscCancelled=true`, `EscMouseProgress=true` e
`BudgetBefore == BudgetAfter` per tutti i dieci boot.
Il kernel non implementa ancora syscall, user mode o filesystem proprio.
