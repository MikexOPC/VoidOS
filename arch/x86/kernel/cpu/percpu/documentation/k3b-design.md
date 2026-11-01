# K3-B — clock sorvegliato per il contesto dei lock

29 ottobre 2026. Scelta ACPI PM approvata dall’utente; implementazione verificata sul BSP; [prove e limiti](k3b-validation.md).
K3-A è chiuso sul BSP; questo blocco mantiene gli AP fermi.

## Problema verificato

Il contesto runtime GS esiste, ma nel bootstrap ordinario Wait è ancora
NULL. La primitiva può acquisire lock liberi; un ticket già prenotato che
non avanza termina diagnosticamente. Prima di autorizzare AP concorrenti
serve un clock utilizzabile anche con IF=0, senza allocazioni, lock o
accesso al contesto dei lock nel callback stesso.

## Scelta proposta: sorgente ACPI PM esistente

Riutilizzare KERNEL_CLOCK e la sorgente ACPI PM già adottata dal progetto.
Ogni contesto CPU avrà un descrittore Wait proprio e stabile; il contatore
hardware può essere comune e letto senza stato software condiviso mutabile.
Avere un clock per contesto non richiede un timer hardware per core.

Fonte primaria: ACPI 6.5 §§4.8.2.1 e 4.8.3.3, Table 4.14:
https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html#power-management-timer-pm-tmr
PM_TMR è read-only, letto a 32 bit; TMR_VAL occupa [23:0], con estensione
[31:24] solo se FADT.TMR_VAL_EXT la dichiara. Frequenza 3.579545 MHz.
Indirizzo, spazio di accesso e capacità provengono dalla discovery; nessuna
porta fissa inventata. Contatore opzionale: assenza o lettura non valida
non autorizzano un fallback silenzioso per l’avvio AP.

L’interfaccia G4 esistente rende sostituibile la sorgente. L’alternativa
è introdurre ora un backend TSC con verifica delle capacità, calibrazione
e ordinamento delle letture: blocco hardware distinto e più ampio. Non
serve scegliere un clock diverso per portare avanti il bring-up BSP.

## Ownership, stati e invarianti proposti

Modulo funzionale: cpu/percpu, con una sottocartella clock per la nuova
responsabilità e test centralizzati rispecchiati. L’ABI dei record K3-A
non cambia per semplice comodità organizzativa.

- Il descrittore runtime viene copiato in memoria statica posseduta dal
  modulo; Read/Context restano un contratto di lifetime esplicito.
- La sorgente deve restare viva, immutabile e leggibile su ogni CPU ammessa.
  Nessun puntatore a stack, vecchio contesto o oggetto revocabile.
- Il callback non prende lock, non alloca, non abilita IRQ e non richiama
  il provider GS. Va verificato l’adattatore effettivo prima di collegarlo.
- Solo BSP e contesto ACTIVE quiescente possono effettuare il primo bind.
  Il punto di bootstrap è dopo la discovery e verifica della sorgente.
- Prima del bind rimane il profilo non conteso K2. Prima di pubblicare
  futuri AP pronti, il loro descrittore deve essere predisposto e provato.
- Timeout e massimo di controlli sono parametri espliciti e finiti; il
  budget temporale deve restare inferiore a metà periodo del contatore.
  Nessuna soglia viene presentata come benchmark universale.
- Se l’installazione o la verifica della sorgente fallisce, nessun bind
  parziale e nessuna autorizzazione all’uso concorrente dagli AP.
- Dopo prenotazione del ticket restano i fail-stop K2: non abbandonare un
  ticket, non continuare dopo clock fermo, guasto o scadenza.

Sequenza concettuale, non eseguibile:

    GS_ACTIVE_UNBOUND -> SOURCE_VERIFIED -> WAIT_BOUND
                              |
                         errore: niente pubblicazione

## Perimetro concordato

Include solo il modulo per-CPU e gli adattatori necessari per recuperare
la sorgente timer esistente, collegarla nel bootstrap, build e test.
La verifica del callback richiede leggere il backend reale pertinente.
Non modifica scheduler, watchdog HPET, politica dei driver o allocatori;
non avvia AP e non introduce nuovi mapping del timer.

## Accettazione

Host: sorgente assente/invalida, alias e lifetime, doppio bind, Depth non
zero, IF0/IF1, overflow, wrap 24/32 bit, clock fermo/guasto e timeout con
ticket prenotato; C ASan, warning come errori e analisi statica pertinenti.
I test del modello restano distinti dal backend privilegiato.

QEMU: dieci boot consecutivi dell’immagine finale con binding verificato,
progressione del contatore con IF=0, contesto GS integro, guard e IF
ripristinati. Test negativi dedicati per arresti previsti, regressione
ordinaria e console. Nessuna dichiarazione di contesa SMP finché gli AP
non eseguono realmente il kernel. Coverage N/D se non misurata.

## Dettagli attuativi

I record GS di K3-A restano invariati. Una tabella statica distinta contiene
64 slot di 64 byte (4 KiB BSS): Wait, copia X86_PM_CLOCK e identità del
contesto associato. Solo lo slot BSP viene collegato. Copiare i campi
numerici elimina il prestito del descrittore FADT temporaneo: il callback
usa solo la copia posseduta, senza riferimenti alla finestra ACPI.

L’adattatore cattura la FADT tramite x86_pm_clock_capture e la finestra
ACPI esistente, poi verifica che la finestra sia inattiva prima del bind.
Il backend corrente supporta il timer PM nello spazio I/O; un timer solo
MMIO o assente non viene emulato inventando una porta. In assenza di ACPI
resta soltanto il profilo BSP non conteso, senza autorizzazione per AP.

La policy di bring-up è 100 ms arrotondati per eccesso (357.955 tick), con
massimo 1.000.000 controlli; prevale il primo limite. Il probe iniziale
richiede progresso entro min(MaxPolls, 4.096) controlli, con IF=0; scarta
salti pari o superiori a metà periodo. Il limite di controlli è un secondo
freno se il clock si ferma, non una conversione calibrata in tempo.

Il bind è unico, quiescente e conserva NextToken. Prima della pubblicazione,
un errore azzera lo slot preparato e permette un nuovo tentativo. Dopo il
bind non esiste API di riconfigurazione o di rilascio della sorgente. I test
di lock che alterano temporaneamente Wait ripristinano il binding runtime.
Il futuro ingresso AP richiederà un contratto separato per pubblicazione,
identità CPU e ordinamento: la tabella da sola non costituisce avvio SMP.
