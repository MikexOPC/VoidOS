# K3-C — registro delle identità CPU prima delle risorse AP

29 ottobre 2026. Scelta A approvata: registro di boot stabile, hotplug successivo; implementazione verificata sul BSP; [prove e limiti](../../../../../../docs/validation/consolidamento.md#checkpoint-per-cpu). K3-A/B
verificano GS e clock del BSP; gli AP restano fermi.

## Riscontri e perimetro

La MADT espone fino a 64 KERNEL_MADT_CPU con Uid, Id, Flags e Kind.
Il preflight APIC verifica già il BSP e fornisce InitialId e Uid; il
profilo operativo attuale è xAPIC. profile.c rileva ma rifiuta x2APIC.
Il record GS possiede soltanto un indice logico zero, non ancora un registro
che lo associ alle identità delle altre CPU. Gli stack/GDT/TSS correnti
sono del BSP, con indirizzi condivisi: non si possono caricare tali
oggetti su tutti i core come se fossero risorse private.

Modulo proposto: arch/x86/kernel/cpu/percpu/identity/.
C per ownership e pubblicazione dello snapshot; Rust in rust/validation/
per regole pure su identità, capacità e corrispondenze. Test centralizzati
in test/arch/x86/kernel/cpu/percpu/identity/, script in scripts/ del modulo.
Sono necessari soltanto adattatori a bootstrap dopo il profilo APIC,
build e test. Nessuna modifica al parser MADT o al controller APIC,
nessuna allocazione/mapping, stack AP, GDT/TSS AP o INIT/SIPI in questo blocco.

## Fonti e confronto

ACPI 6.5 §5.2.12.2, Tables 5.22/5.23: Local APIC record type 0,
UID offset 2 (8 bit), APIC ID offset 3 (8 bit), Flags offset 4 (32 bit).
Enabled è bit 0; Online Capable è bit 1 e deve essere zero quando Enabled
è uno. Con entrambi zero il processore è inutilizzabile e il contenuto
va ignorato secondo la specifica. Riferimento del parser, non nuovi
accessi al firmware in questo modulo.
https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#processor-local-apic-structure

ACPI 6.5 §5.2.12.12, Table 5.34: Local x2APIC type 9, ID a 32 bit offset
4, Flags offset 8, UID offset 12. Un ID x2APIC non va troncato a 8 bit.
Il nuovo registro non introduce un backend x2APIC tramite una conversione.
https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#processor-local-x2apic-structure

Linux v6.12, arch/x86/kernel/cpu/topology.c: mantiene mapping distinti
cpu index → APIC ID e ACPI ID, riserva CPU #0 al processore di boot,
distingue CPU assegnate, disabilitate e rifiutate. Hotplug ha un percorso
separato. Riferimento concettuale, non codice trasferito; GPL-2.0-only.
https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/cpu/topology.c

## A: registro di boot stabile, hotplug successivo (proposta)

- Snapshot posseduto statico, massimo 64 CPU senza tagliare silenziosamente
  gli input. Nessuna dipendenza dall’heap o dal prossimo mapper degli stack.
- Tre identità distinte: indice logico interno, APIC ID, UID ACPI.
  BSP all’indice zero solo dopo matching esatto con il profilo verificato.
  Non assumere APIC ID zero o BSP primo elemento della MADT.
- Le CPU abilitate dal firmware sono candidate al futuro bring-up;
  i record solo Online Capable sono contati separatamente, senza indice
  operativo o tentativi di accensione. I record inutilizzabili sono ignorati.
- Il backend attuale resta xAPIC; identità non rappresentabili o modalità
  non supportate non vengono convertite né dichiarate avviabili. Il
  progetto del backend x2APIC rimane distinto, con errori espliciti.
- Rifiutare ambiguità tra identità utilizzabili: UID/APIC ID duplicati,
  BSP assente o non abilitato, profilo incongruente e capacità superata.
- Indici AP stabili per questo boot: dopo il BSP, ordine dei record
  utilizzabili della MADT. Nessun requisito di APIC ID consecutivi.
- Pubblicare una sola volta dopo validazione completa; conservare copia
  dei campi necessari, senza puntatori alla struttura temporanea in input.
- Gli stati del registro sono identità scoperte, non CPU in esecuzione.
  Il contatore reale dei core pronti resta uno; nessun flag AP_READY nuovo.
- Un input invalido non produce snapshot parziale e non modifica quello
  pubblicato; nessuna riconfigurazione dopo la prima pubblicazione.

Sequenza concettuale, non eseguibile:

    MADT + BSP_PROFILE -> validate -> stable identity snapshot
                            |
                      error: no publication

L’assegnazione di risorse a questi indici arriva nel blocco successivo:
stack con guardie RW/NX, GDT/TSS/IST privati e ownership con rollback prima
dell’avvio. Layout, budget e regole di rilascio richiedono il proprio
contratto prima di introdurre i mapping; gli indirizzi BSP non sono un template
hardware valido da copiare su ogni CPU.

## B: includere già il ciclo di hotplug

Oltre al registro iniziale richiede policy per CPU inizialmente offline,
abilitazione firmware/namespace ACPI, aggiunta/rimozione e lifetime dei
riferimenti, risorse per CPU riconfigurabili, notifiche e sincronizzazione.
Il bit Online Capable da solo non costituisce una procedura di attivazione.
Questo allarga il perimetro oltre il bring-up statico K3 e richiede un
progetto dedicato prima dell’implementazione.

## Criteri di accettazione

Rust: input vuoto, capacità 64/oltre limite, ID sparsi, BSP non primo/ID
non zero, UID/APIC ID duplicati, flags, CPU offline e modalità non supportate;
nessuna unsafe nella logica pura. C: ownership, alias, pubblicazione unica,
output immutato su errore, IF e snapshot indipendente dagli input.

Host C con ASan e warning come errori; test Rust debug/release e analisi
statica. QEMU: almeno dieci boot consecutivi del profilo finale con
1/4 CPU configurate, confronto tra MADT e snapshot, indice BSP coerente
con GS, numero CPU abilitate corretto e un solo core realmente pronto.
Regressioni del clock/contesto BSP, bootstrap e profili pertinenti.
Nessuna prova di avvio AP da una sola enumerazione.

La fase documentale precedente alla scelta non includeva prove runtime.
I risultati dell’implementazione sono riportati separatamente nella validazione K3-C.

## Vincoli riscontrati nell’implementazione

Il parser MADT esistente ammette Flags 1 oppure 2 e controlla unicità anche
sui record offline. Non viene modificato qui: record Flags=0 vengono ignorati
dal pianificatore puro, ma sono rifiutati dal percorso di cattura precedente.
Questo non amplia la compatibilità del parser né implementa hotplug.

Il registro conserva il tipo MADT originale. Record type 9 con ID basso
possono essere rappresentati senza truncamento nel profilo xAPIC verificato;
ID abilitati >=255 sono rifiutati, non rimappati. Intel SDM combinato 089US,
Vol.3A §12.6.2.1: destinazioni fisiche xAPIC unicast 00H–FEH, FFH broadcast.
https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf

La cattura copia 1.056 byte in BSS, con piano temporaneo della stessa taglia
sullo stack BSP. Mutazioni e lettori del registro sono BSP-only e preservano
IF. Ready non è un protocollo atomico di pubblicazione verso AP: tali core
restano esclusi e il futuro ingresso dovrà definire il proprio ordinamento.
La ricerca duplicati costa O(n²), con n massimo 64 (2.016 confronti di coppia).
Non dipende da APIC ID consecutivi e non alloca array grandi quanto l’ID.
