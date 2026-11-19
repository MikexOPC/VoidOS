# R-003 — riferimento delle istruzioni OS

Aggiornato il 5 ottobre 2026. Queste regole correggono gli esempi delle direttive locali, SKILL.md e REVIEW_INSTRUCTION.md; non descrivono nuove protezioni implementate nel kernel.

## Registri e prerequisiti

| Funzione | Controllo | Prerequisito |
|---|---|---|
| Paging | CR0.PG, bit 31 | Tabelle e transizione valide |
| Write protection | CR0.WP, bit 16 | Permessi R/W coerenti |
| NX/XD | IA32_EFER.NXE, bit 11; XD delle entry, bit 63 | CPUID(80000001H).EDX[20] |
| SMEP | CR4[20] | CPUID(7,0).EBX[7] |
| SMAP | CR4[21] | CPUID(7,0).EBX[20] e politica delle copie utente |

Verificare prima il massimo leaf CPUID disponibile. Conservare i bit esistenti con maschere uint64_t; non impostare bit riservati. NX non e un flag CR0. SMEP impedisce fetch supervisor da pagine user; SMAP limita accessi dati supervisor alle pagine user. U/S controlla l'accesso utente alle pagine supervisor. Con SMAP, AC/STAC/CLAC richiedono finestre autorizzate e cleanup anche su fault.

Fonti: [Intel SDM Vol. 3A](https://cdrdv2-public.intel.com/843836/325384-sdm-vol-3abcd-dec-24.pdf), §§2.5–2.5.1 (registri/capabilities), 5.6 (permessi paging), 6.13.1–6.13.2 (NX).

## Transizioni e interrupt

Una near CALL non cambia CPL: in ring 0 chiamare una funzione kernel e normale. Una chiamata da ring 3 non aggira i permessi. Progettare entry/return e stack per il meccanismo prescelto; SYSCALL non effettua automaticamente lo switch a uno stack kernel. SYSRET non e un ritorno universale: verificare stato/indirizzi ed eventuale percorso IRETQ. Salvare il contesto interrotto in Assembly prima del prologo C, poi rispettare ABI e politica SIMD.

Fonte: Intel SDM Vol. 3A, §§6.8.8 (SYSCALL/SYSRET), 7.14 (interrupt in IA-32e); il nome di una istruzione trovato con grep non dimostra sicurezza.

## KPTI e puntatori utente

KPTI riduce l'esposizione delle mapping kernel rispetto a Meltdown su CPU vulnerabili; non risolve tutte le varianti Spectre. La vista utente mantiene le mapping minime necessarie a entry/exit; U/S e validazione delle copie restano obbligatori. Applicabilita e mitigazioni dipendono da CPU, microcode e confini di fiducia. Senza processi utente, segnare KPTI come non implementato/gate futuro, non come exploit dimostrato.

Fonte: [Linux PTI, overview/design](https://docs.kernel.org/arch/x86/pti.html). Una copia utente deve limitare sorgente e capacita destinazione, gestire overflow, permissions, pagine assenti e fault durante la copia. Un controllo numerico seguito da memcpy non basta.

## Regole operative coerenti

- Massimo 400 righe per sorgente e cinque file diretti per cartella; dimensioni inferiori sono obiettivi locali, non altri limiti bloccanti. Documenti/report non sono sorgenti compilati.
- C/header nella cartella funzionale; C++/Rust/Assembly nelle rispettive sottocartelle. Test in test/, script in scripts/, artefatti in build/. Conservare arch/x86 attuale: gli esempi arch/x86_64 non impongono un rename.
- Un modulo funzionale per task, inclusi i suoi test/interfacce e la documentazione necessaria. Per una task solo documentale: review delle fonti, coerenza, link e diff; build, leak test e QEMU N/A. I gate hardware restano obbligatori quando cambia codice eseguibile.
- Dieci boot significano dieci successi consecutivi dello stesso profilo positivo verificato. Fault previsti di test negativi sono distinti; nessun numero di test equivale alla copertura percentuale.
- ASan controlla accessi invalidi; non certifica leak fisici UEFI. Usare accounting delle risorse e un leak detector dove supportato, indicando limiti e prove non eseguite.
- Avviare il progetto UEFI con OVMF/ESP e gli script supportati; non assumere supporto QEMU per -timeout o -kernel di un ELF arbitrario. Deadline/terminazione sono responsabilita del test host. [QEMU invocation](https://www.qemu.org/docs/master/system/invocation.html).
- Negli esempi: nessun OK prima del controllo reale; etichette colorate soltanto, separatori su righe distinte. Nessun template con caselle gia spuntate costituisce un risultato.

## Esempi rimossi e decisioni rinviate

Gli snippet NIC 82599 con offset/bit/registri non verificati sono rimossi: un driver richiede un datasheet revisionato, PCI identity, BAR 32/64-bit e dimensione, mapping/cache policy, sequenza reset e interrupt del device esatto. Un test e1000 non valida un driver 82599. Volatile non sostituisce ordering, barriere, sincronizzazione o politica MMIO.

Un timeout deve avere clock, deadline, wake e recovery appropriati alla periferica; un contatore unsigned post-decrementato puo andare in wrap. Nessun timeout universale di 100 ms. Per spinlock/SMP: lock order e ownership, non saltare silenziosamente una reacquisizione.

Tempi Linux/Minix e target boot nei template sono esempi non misurati; rimuovere benchmark senza workload/fonte. Il confronto CFS e storico, non descrive automaticamente lo scheduler Linux corrente. Nessun algoritmo futuro obbligatorio prima di misure e requisiti.

## Esito documentale

R-003 corretto nei tre documenti guida. Le full review precedenti rimangono snapshot storici con le loro location originali. Nessuna modifica al codice CPU/driver, nessuna nuova prova QEMU o leak dichiarata da questa task. Restano aperte R-001 (liveness timer) e le altre priorita non chiuse separatamente.
