# K3-I — panic coordinato su quattro core

## Risultato

Il backend esistente arresta il gruppo diagnostico completo: BSP e tre AP
realmente inizializzati, non soltanto configurati in QEMU. Dieci boot per
origine passati senza errori inattesi: chiamata volontaria BSP oppure
eccezione NMI su AP3. Il [design](k3i-group-panic-design.md) riusa il protocollo
NMI diretto approvato; il codice aggiunto è un innesco esclusivamente di test.

| Prova finale | Esito | Evidenza sotto `kernel/build/` |
|---|---|---|
| Origine BSP, targets=ACK=14 | 10/10 | `group-panic-804e51d110b64ae591e86840e26916cd/results.json` |
| Origine AP3, targets=ACK=7 | 10/10 | `group-panic-a45b3079d2864c6f8e6356c9409b8c7f/results.json` |
| Regressione sequenziale senza panic | 10/10 | `first-ap-firmware-ef39759b5dc144d79dfa99ef1658b953/results.json` |
| Coordinatore Rust | 6 debug + 6 release | `k3i-group-panic-host.log` |
| Record C/ASan, otto produttori concorrenti | 10 esecuzioni | `k3i-group-panic-host.log` |
| Trasporto C/ASan | 11 scenari | `k3i-group-panic-host.log` |
| Output C/ASan | 10 scenari | `k3i-group-panic-host.log` |
| Codice macchina emergenza, entrambe le immagini | 11 oggetti per immagine senza FP/SIMD rilevati | `k3i-group-panic-{bsp,ap}-v1/*.asm.txt` |
| Cppcheck dell'innesco | Nessuna diagnostica bloccante | `k3i-group-panic-static/cppcheck.xml` |

Il monitor controlla un reporter, tre conferme, missing=outcome=0, quattro
record completi e classificazione origine/follower. Legge i registri reali
di ogni core, CR3/NX/WP/IF, GS, GDT/TR/IDT degli AP, stack principale
interrotto e IST2 di arresto. Verifica anche frame privati AP, contenuto
immutabile del trampoline e delle tabelle, lease pinned e budget fisico.
Il BSP conserva l'IDT iniziale verificata; gli AP usano l'IDT finale condivisa.

La seriale viene scaricata prima dello stop della VM; il test attende poi
HLT e il marcatore distinto del reporter o follower su tutti i core.
Un fallback BSP durante il caso AP non soddisfa il controllo dell'origine.
Le due prove preliminari da un boot non sono sommate alle campagne finali.

## Uso

Compilare con `kernel/scripts/build.ps1 -TestApLaunch -TestApSequence
-TestApTransition joint-panic -TestApGroupPanic bsp` (oppure `ap`), indicando
una directory di output. Eseguire il Python del progetto con
`test/arch/x86/kernel/cpu/percpu/panic/integration/group.py --image
<output>/kernel.elf --origin bsp --boots 10`, oppure `--origin ap`.
Il profilo richiede quattro vCPU; il runner prepara una ESP privata e
termina soltanto la propria VM. Senza il flag, l'innesco non viene linkato.

## Memoria, fonti e limiti

Nessuna nuova allocazione guest. Le risorse sigillate restano trattenute
dopo il panic, come richiesto dal contratto. Il monitor verifica accounting
e pinning; non equivale a un leak detector della RAM fisica. ASan copre
il C host, non Rust, Assembly o memoria guest. Valgrind, TSan, coverage
linee/branch e hardware fisico N/D.

Cppcheck conserva informazioni sugli include di sistema e sui limiti
dell'analisi dei branch. La suite host conserva due suggerimenti di stile
preesistenti nei test: un reset di ID sovrascritto dal caso successivo e
due chiamate consecutive a retire che verificano la transizione di stato.
Nessuna soppressione aggiunta. Compilazioni C
con /W4 /WX e Rust con -D warnings. Fonte hardware:
[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
§11.6.1/figura 11-12 per ICR e §6.7.1 per blocco NMI fino a IRET.

È una prova deterministica QEMU/TCG/OVMF del gruppo parcheggiato: non
dimostra fault simultanei, task AP, hotplug, guasti hardware o SMP ordinario.
K3 resta aperto per integrazione ordinaria e stress finale.
Manifest delle immagini, evidenze e sorgenti del blocco:
`kernel/build/k3i-group-panic-manifest.json`.
