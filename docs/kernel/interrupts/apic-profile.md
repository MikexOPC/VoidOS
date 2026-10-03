# Preflight BSP, xAPIC e PAT

Il kernel verifica lo stato CPU prima di costruire mapping MMIO.
Questo modulo non scrive MSR, non accede ai registri MMIO dei
controller e non abilita interrupt. Il profilo corrente richiede
xAPIC gia globalmente abilitato, CPU BSP e IF=0.

## Cattura e verifica

Il lettore assembly preserva RBX e RDI secondo l'ABI Microsoft x64.
Controlla CPUID leaf 1 e le feature MSR, APIC e PAT prima di RDMSR.
Verifica anche la disponibilita di CPUID 80000008h e il limite fisico
32-52 bit. Legge soltanto IA32_APIC_BASE (1Bh) e IA32_PAT (277h).
Gli offset del buffer sono controllati con assert statici nel C.
Un errore puo lasciare parziale lo snapshot locale, mai pubblicato.

Rust verifica bit BSP, global enable, modalita xAPIC/x2APIC,
intervallo fisico e codifiche PAT. Il profilo iniziale limita il
campo base LAPIC a 36 bit, comunque entro MAXPHYADDR. x2APIC viene
rilevato ma respinto: non si tenta una transizione a xAPIC.

Il C confronta la base MSR con MADT e associa l'Initial APIC ID
CPUID al record CPU Enabled della topologia. UID, APIC ID e indice
del record non vengono confusi. Online Capable senza Enabled non
basta. Record MADT non supportati e assenza di I/O APIC impediscono
questo preflight per il percorso IRQ iniziale.

Il bit BSP prova lo stato dichiarato dal processore. L'Initial APIC ID
non prova che il firmware non abbia cambiato l'ID attuale del LAPIC:
prima del routing sara necessario leggere e confrontare anche quel
registro MMIO. Il modulo non annuncia controller configurati.

Due catture devono concordare su CPUID, MSR e MAXPHYADDR. I flag
aritmetici transitori possono cambiare; IF deve restare zero.
Questa verifica assume esecuzione UP e non introduce sincronizzazione SMP.

## Cache policy per la futura foglia da 4 KiB

Si verifica ogni byte PAT: tipi UC, WC, WT, WP, WB e UC-minus;
codifiche riservate vengono respinte. PA0 deve essere WB, poiche
i mapping RAM esistenti selezionano lo slot zero. Si sceglie il
primo slot UC forte disponibile, distinguendolo da UC-minus.
Il PAT ereditato non viene modificato.

Per una foglia da 4 KiB l'indice seleziona PWT (bit 3), PCD (bit 4)
e PAT (bit 7). Il modulo conserva soltanto questi bit di cache;
non costruisce o pubblica una PTE. Anche uno slot UC in PA4-PA7
viene gestito, senza assumere il valore predefinito PA3.

La scelta PAT da sola non autorizza un mapping: nel prossimo blocco
vanno controllati regione fisica, riserve, alias e interazione con
MTRR. Non si puo riusare direttamente il lettore ACPI per MMIO:
quello legge RAM WB, mentre il LAPIC richiede UC e accessi a 32 bit
con offset allineati a 16 byte.

## Ownership e costo

La bozza viene pubblicata solo dopo tutte le verifiche. I buffer
tipizzati richiedono memoria valida, allineamento 8 e dimensioni
note; output e input devono essere disgiunti. I fallimenti del
piano mantengono invariato l'output. Il profilo persistente occupa
80 byte di BSS del kernel, senza allocare frame o oggetti arena.
Le due catture occupano 96 byte temporanei; la bozza 80 byte.

Il consumer puo ricostruire e confrontare tutti i campi derivati.
Questa verifica strutturale non autentica il firmware o lo snapshot
e non sostituisce una nuova lettura hardware prima di configurare IRQ.
Se fallisce il logging dopo la pubblicazione, il kernel si arresta
con un profilo valido ancora posseduto dal kernel.

## Verifica

Test C: BSP non primo nella MADT, UID distinto da ID, CPU Enabled
e Online Capable, base incoerente, feature mancanti, x2APIC,
bit riservati, PAT non standard, output invariato e alias,
due catture incoerenti e riuso del profilo gia pubblicato.
Tre test Rust coprono le modalita, tutti i 256 valori PAT nei
sette slot non zero e gli otto encoding PAT/PCD/PWT.

La campagna QEMU legge il profilo dal monitor, lo confronta con
i log e controlla IF, CPUID, base MADT e selezione UC. Viene
conservato anche il dump indipendente `info lapic` per la revisione.
Non sono eseguiti test su hardware fisico.

Il dump OVMF/QEMU contiene impostazioni LAPIC gia presenti, incluse
rotte LVT e uno spurious vector scelto dal firmware. Non costituiscono
una configurazione IRQ del kernel. Prima di STI occorrono vettori,
handler con ritorno e maschere controllati dal kernel.

## Riferimenti

[Intel SDM, Vol. 3A: 11.4.4, 11.4.6, 11.12 e capitolo 12;
Vol. 4: IA32_APIC_BASE e IA32_PAT](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
