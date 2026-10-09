# Review breve e ownership degli stack task

Rapporto storico del blocco risorse. Il mapping qui indicato come futuro e ora
implementato: vedere [integrazione e prove finali](integration.md).

8 ottobre 2026. Modulo funzionale: scheduler, adapter x86 e relative prove.
Stato: protocollo risorse implementato, mapping hardware NON ancora implementato.
Leggere con [backend](backend.md) e [design](../../../../../kernel/scheduler/documentation/design.md).

## Review del blocco precedente

Riletti C, layout, Assembly e test. Verificati shadow space e allineamento,
GPR/XMM non volatili, formato FXSAVE64, controllo MXCSR, scrubbing del template,
CPL prima di CR/MSR e assenza di scritture di controllo CPU.
Ricontrollate le fonti [Microsoft ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)
e [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html),
con le copie revision093 gia registrate nel report backend.

Nessuna contraddizione hardware/ABI trovata nei percorsi esaminati.
Due miglioramenti applicati al predicato CPU, con test di regressione:

- Richiedere CR0.PE, bit0, oltre a WP16/PG31 (Vol3A §2.5).
- Respingere CR3 atteso >=2^52, limite architetturale della base nel formato
  ordinario a quattro livelli (§5.5.2). La larghezza fisica effettiva della CPU
  deve continuare a essere verificata dal bootstrap; non e dedotta da questo limite.

Queste correzioni rendono piu rigorosi gli snapshot simulati. Non sono prove
di un crash o di uno sfruttamento nel kernel. Restano i limiti gia dichiarati:
raw switch privato, task kernel fidati, niente unwind/TLS/preemption/SMP;
prerequisiti IRQ/NMI e mapping reali da garantire nell'integrazione.

## Nuovo protocollo risorse

`stacks.h`/`stacks.c` possiedono owner stabili da72 byte e lease fisiche esatte.
Tre stati nativi: LEASED, MAPPING (anche parziale), READY. L'indice e la generazione
devono coincidere con l'handle Rust RESERVED. Il record puo essere azzerato
soltanto dopo revoca confermata, invalidazione nel port, unpin e release riuscita.

Acquisizione:

1. Owner vuoto, buffer separati, pool valido, dispatcher e slot vuoto verificati.
2. Allocate16 pagine, conferma lease unica nell'inventario, limiti fisici32..52.
3. Pin confermato; entrare MAPPING prima del primo tentativo di pubblicazione.
4. Map/readback, zero/readback, poi READY e validazione completa.

Rilascio:

1. Verifica self, lease, indice/generazione e contesto dispatcher.
2. Revoca full/partial, verifica slot assente prima di unpin/free.
3. Unpin e release confermati; soltanto allora azzera l'owner.

Un guasto conserva il record e le risorse ancora possedute. Il ritorno falso
da acquire non implica owner vuoto: il dispatcher deve verificare cleanup prima
di ABORT. Se cleanup fallisce, fermare il profilo e conservare RESERVED/FINISHED.
Gli ID fisici e le generazioni non vengono riavvolti. Il protocollo non recupera
da page fault e non e una capability per chiamanti non fidati.

## Geometria e contratti ancora aperti

Guardia iniziale0x700000140000, 8 slot da18 pagine: guardia, 16 dati, guardia.
Foglie PT320..463; stessa PT del bootstrap. Gli assert verificano che il layout
segue arena128..263, stack principale96..113 e LAPIC288.
Sono indirizzi scelti dal progetto, non costanti hardware Intel.
Costo massimo512KiB fisici e576KiB virtuali; nessuna nuova page table prevista.

L'assenza di collisioni con questi layout NON prova che le PTE siano libere.
Il port deve verificare tutta la fascia interessata e ownership dei frame,
CR3/ricorsione/genitori/NXE/WP, dati supervisor RW/XD, guardie esattamente zero,
solo A/D tollerati, invalidazione locale prima del riuso e zeroing completo.
Un port revoke non puo cancellare PTE estranee o corrotte.

Le cinque funzioni platform dichiarate in stacks.h non hanno implementazione
hardware e non sono collegate al kernel. I test usano mock espliciti di mapping
e allocatore. La chiamata alle API dell'allocatore e implementata nel manager,
ma non e stata eseguita qui con l'allocatore fisico reale.
Nessuna scrittura PTE, nessun INVLPG e nessuna modifica all'immagine kernel.

## Prove finali dopo tutte le modifiche

Evidenza: `kernel/build/scheduler-context-c63b049db80242fe8556f93ad5c6a6d7/`.
Riproduzione: `arch/x86/kernel/scheduler/scripts/test.ps1`.

| Prova | Esito |
|---|---|
| Stato/CPU, ASan C | 10 esecuzioni x55.700 =557.000 controlli |
| Switch reale Windows | 10 x4.004 =40.040 switch, pattern GPR/XMM/FCW/MXCSR e canary |
| Guardie Windows | 40 access violation previste; 20 riserve rilasciate |
| Risorse, ASan C | 10 x361.000 =3.610.000 controlli; allocatore/PTE MOCK |
| Workload risorse | 1.000 cicli per esecuzione; otto slot simultanei, 11 scenari guasto |
| Errori | OOM, pin, map prima/parziale, zero, revoke/unpin/free, guasti combinati e retry |
| Identita | Owner copiato, handle errato, lease alterata, dispatcher negato |
| Compilazione | MSVC/MASM warning come errori, zero warning |
| Cppcheck | Zero error/warning/performance/portability; 6 style e13 information documentati sotto |
| Dimensioni | Max223 righe nei sorgenti del backend; max5 file diretti |
| Integrita prove | Hash dei12 sorgenti/script finali verificati dopo la suite |

I sei style riguardano tre parametri dei mock che rispettano firme non-const
esistenti, due fasce guardia del fixture e il callback chiamato da Assembly.
Le informazioni riguardano header di sistema non caricati e checkers disponibili.
Non sono stati nascosti con suppression. Tutti i diagnostici sono in cppcheck.xml.
I disassemblati C/Assembly esaminati non presentano AVX/AMX; il controllo non
copre l'intero futuro kernel o i runtime esterni. ASan non strumenta Assembly;
il test di switch non usa ASan/fiber hooks.

L'accounting simulato ritorna al budget iniziale dopo cleanup riuscito e conserva
il debito durante cleanup fallito. Non misura leak RAM fisica. Valgrind non
eseguito, copertura linee/branch N/D, ring0 mock, zero boot QEMU per questi moduli.

## Prossimo gate

Implementare e verificare il port PTE nel medesimo modulo scheduler; quindi
collegare owner risorse, contesti e dispatcher Rust/C. Prima di dichiarare pronto
il modulo: almeno dieci boot QEMU dell'immagine finale, controlli PTE/RSP/CPU,
accounting dell'allocatore reale e prove negative guard/NX/revoca. Nessun
passaggio a preemption o nuovi driver sulla base dei soli risultati host.
