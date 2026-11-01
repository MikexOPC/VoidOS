# K3-I — primo AP reale, 1 novembre 2026

## Risultato e perimetro

Il diagnostico avvia davvero l'AP di indice 1 con INIT/SIPI e un NMI
successivo alla disponibilità dell'IVT. Esegue prefisso, ammissione C,
provider per-CPU e ritorno IRETQ del prodotto, quindi parcheggia l'AP.
Il BSP rimane supervisore. Due CPU inizializzate nel kernel; quattro vCPU
configurate non significano quattro CPU ammesse. Lo scheduler resta BSP.
Il boot ordinario non avvia gli AP. Questo risultato non chiude K3.

Si riusano risorse sigillate e trattenute dal commit congiunto. Prima
dell'avvio si verifica l'immagine completa; dopo l'avvio si leggono soltanto
il Gate allineato e gli stati pubblicati. Nessuna scansione del buffer
dati mentre l'AP lo modifica, nessun rilascio o nuovo tentativo dopo errore.
Il negativo senza NMI mantiene l'AP in modo reale, lo mette in quarantena
e conserva le lease pinned. Il diagnostico termina intenzionalmente.

## Correzioni trovate durante il ciclo

- Il primo test firmware ha raggiunto PARKED, ma il verificatore confrontava
  male Accessed/Dirty delle tabelle. Ora ignora soltanto i bit 5/6 su entrambi
  i valori; indirizzi, permessi e foglie supplementari restano verificati.
  Evidenza iniziale conservata in
  `kernel/build/first-ap-firmware-2da8dd3ab8c047029b7418bfbc812013/`.
- Il test host `late-ready` ha riprodotto una race: la fase locale letta
  prima di PARKED poteva essere precedente alla pubblicazione dell'AP.
  Il supervisore rilegge la fase dopo l'osservazione acquire di PARKED.
  Rosso conservato in `kernel/build/k3i-first-ap-host-red/`; verde sotto.
- Non si richiama il sigillo dell'intera pagina dati dopo INIT: stack e
  Gate sono mutabili. La lettura Gate è DWORD, RO/NX e con prestito posseduto.

## Prove finali

| Prova | Risultato | Evidenza sotto `kernel/build/` |
|---|---|---|
| Supervisore C e coordinatore Rust reali; I/O e completamento CPU modellati | 9/9, C con ASan | `k3i-first-ap-host-v2/tools.log` |
| Trasporto INIT/SIPI, errori e clock modellati | 41/41, C con ASan | `k3i-first-ap-wake-final/results.json` |
| Primo AP UEFI/OVMF, immagine positiva v2 | 10/10 consecutivi | `first-ap-firmware-451593f7f1dc420b9b53d04916dfa679/results.json` |
| NMI omesso intenzionalmente, immagine negativa v2 | 1/1 | `first-ap-firmware-f3faeb25785247fd932f301e15501cb4/results.json` |
| Regressione sigillo joint-panic, AP fermi | 10/10 consecutivi | `ap-joint-firmware-bf56e06a8a5b4fe991e03e9f87c00990/results.json` |
| Regressione boot ordinario, AP fermi | 10/10 consecutivi | `percpu-firmware-565c54e793954df8902ac706246a7beb/results.json` |

Il monitor controlla registri reali del core 1: CR3 operativo, GS privato,
RSP sul frame privato, GDT/TR e IDT definitive, WP/NX/SSE e IF=0.
Controlla inoltre PARKED, fase provider 2, maschera panic 3, record privato,
codice basso immutato, permessi delle tabelle, guardie, budget e pin delle
lease. Gli altri due AP restano PREPARED. Salva i registri osservati;
in caso di errore raccoglie quelli di tutti e quattro i core.

MSVC /W4 /WX e Rust -D warnings nelle compilazioni; Cppcheck 2.22.0 senza
error/warning/performance/portability bloccanti. Informazioni sugli include
e suggerimenti di stile restano nei report XML, non sono presentati come
assenza assoluta di diagnostiche. Analisi nativa in `k3i-first-ap-static-final/`.
Audit dei 16 file del blocco in `k3i-first-ap-source-audit.json`: massimo
385 righe, nessun tab/spazio finale, cartelle sorgenti interessate massimo
quattro file diretti. Manifest immagini e prove: `k3i-first-ap-manifest.json`.

I runner firmware finali terminano con exit code 0. Il wrapper della build
joint è stato chiuso dopo la generazione dell'ELF e la sua campagna 10/10;
il suo exit code d'interruzione non è contato come build PASS. Le build
positive v2, negativa v2 e ordinaria hanno terminato con exit code 0.

## Sicurezza e limiti

Nessuna allocazione dinamica nuova nel supervisore: usa metadati statici,
risorse trattenute e prestito temporaneo già esistente. Il budget guest e
il pinning vengono verificati, non si dichiara un leak detector fisico.
ASan riguarda il C host; Rust/Assembly e RAM guest non sono strumentati.
Valgrind, TSan, coverage linee/branch, hardware fisico e carico SMP operativo
sono N/D. Dieci prove non dimostrano assenza universale di race.

Profilo fidato QEMU/TCG/OVMF, xAPIC e un AP soltanto. Il limite di un secondo
è diagnostico, non un requisito universale. L'errore 5 del supervisore può
indicare anche clock/guardia/polling: solo il negativo controllato dimostra
il timeout senza NMI. Un'incoerenza scoperta dopo PARKED resta terminale;
il coordinatore non viene riportato indietro da PARKED a QUARANTINED.

## Fonti e prossimi gate

[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§9.1.1 per DWORD allineato atomico; §9.2.2 per ordine WB; §9.4.4.1,
tabella 9-1, per INIT/SIPI e attese; §11.5.3 ESR +280h;
§11.6.1 figura 11-12 per ICR +300h/+310h, destination[31:24], delivery
status12, modalità[10:8], assert14 e trigger15. NMI 4400h, INIT 4500h,
SIPI 4600h|vettore. §§6.7.1 e 6.14 per blocco NMI, IDT/IST e IRET.
Il mapping LAPIC UC è quello posseduto dal backend panic.

Restano: riuso controllato della transizione solo dopo PARKED, avvio
sequenziale degli altri AP, prove NMI del panic remoto con AP reali e
integrazione nel boot ordinario. Nessuno scheduler SMP o userspace aggiunto.
