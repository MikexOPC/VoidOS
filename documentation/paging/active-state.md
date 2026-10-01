# CR3 e stato di paging del firmware

Il loader ora legge CR3 prima e dopo la costruzione delle proprie tabelle.
Le letture privilegiate sono in `arch/x86/lib/processor/processor_access.c`;
`cpu/processor_query.c` le adatta al contesto UEFI x64.
I test Windows collegano invece un modello dei
registri: nessuna istruzione privilegiata viene eseguita nel processo host.

Si leggono anche CR0, CR4 e IA32_EFER. Prima di RDMSR il chiamante deve
avere capacita CPU valide, con long mode e NX gia rilevati. Il controllo
non installa un gestore per eccezioni causate da firmware/hypervisor
non conformi; CPUID e il contratto UEFI rimangono presupposti del lettore.

`arch/x86/lib/processor/paging_state.rs` valida solo numeri, in Rust no_std. Il profilo
richiede CR0.PE e PG, CR4.PAE, EFER.LME e LMA. Rifiuta CR4.LA57 attivo:
le nostre tabelle sono a quattro livelli, e una transizione da cinque
livelli richiederebbe un percorso dedicato. Rifiuta anche bit di CR3
oltre la larghezza fisica rilevata. Funzionalita che usano bit alti di
CR3, come altri profili di address masking, non sono supportate qui.

La root viene estratta eliminando i 12 bit inferiori, senza dereferenziare
l'indirizzo fisico. Con PCIDE attivo quei bit contengono il PCID;
senza PCIDE includono PWT/PCD e bit ignorati. Non si interpreta tutto CR3
come un indirizzo di pagina, e non si impone che il valore grezzo sia
allineato. Una root fisica zero e ammessa dal decoder numerico: questo
decoder non alloca ne possiede le tabelle del firmware.

## Diagnostica

Il boot stampa valori con 16 cifre esadecimali:

- CR3 firmware prima della costruzione;
- flag NXE=1, WP=2 e PCID=4, combinati con OR;
- CR3 firmware dopo la costruzione;
- root fisica delle nuove tabelle, ancora inattive.

La fotografia riporta NXE e WP effettivi, separati dalle capacita CPUID.
Un flag disattivato non viene abilitato da questo passo. Il confronto
richiede due risultati validi e uguaglianza di CR3 grezzo, root e flag.
Una modifica provoca EFI_DEVICE_ERROR e la pulizia delle risorse.
Anche il lettore reale ricontrolla CR3 alla fine della singola fotografia.

Queste osservazioni non sono una fotografia atomica di tutti i registri
e non provano che il contenuto delle tabelle del firmware sia immutabile.
Il codice non scrive CR3, CR4, EFER o CR0 e non modifica RSP.

## Verifiche

Tre test Rust verificano il modo obbligatorio, gli otto stati dei flag,
larghezze fisiche invalide, bit fuori limite e metadati inferiori di CR3.
I test C verificano estrazione della root, invalidazione, confronto e
formato esadecimale, inclusi i limiti del buffer di stampa. I test di
ingresso simulano LA57 attivo e CR3 modificato dopo le allocazioni,
controllando errore e pulizia.

QEMU confronta anche i valori stampati: CR3 prima e dopo deve coincidere,
mentre la root delle tabelle inattive deve essere distinta e allineata.
Gli altri scenari firmware continuano a essere verificati.

Il cambio CR3 richiede prima una transizione che resti mappata, uno
stack utilizzabile, gestione eccezioni e un kernel eseguibile con ABI
definita. Le tabelle attuali contengono kernel, pagina del contratto e stack.
Il kernel diagnostico viene caricato ma non eseguito; le tabelle restano
inattive.

Fonti: [Intel SDM Volume 3A, sezioni 4.1 e 4.5](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
[Microsoft, __readcr3](https://learn.microsoft.com/en-us/cpp/intrinsics/readcr3?view=msvc-170),
[Microsoft, __readmsr](https://learn.microsoft.com/en-us/cpp/intrinsics/readmsr?view=msvc-170).
