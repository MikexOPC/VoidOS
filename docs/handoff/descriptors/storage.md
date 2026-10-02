# Risorse persistenti dei descrittori

Il boot ordinario prepara un blocco GDT/TSS/IDT in pagine possedute,
costruisce le sue mappature e aggiorna il preflight. Le tabelle restano
inattive: questa fase non esegue LGDT, LIDT, LTR o cambio CR3.

## Proprieta e disposizione

`BOOT_DESCRIPTOR_STORAGE` possiede nove pagine contigue EfiLoaderData:
36 KiB, comprendenti il blocco di 704 byte e due stack di 16 KiB.
L'owner vive nel chiamante; `BOOT_EXIT_HANDOFF.Descriptors` lo prende
in prestito. Tutte le risorse devono vivere fino al loro ultimo utilizzo.

| Risorsa | Offset fisico dalla base | Indirizzo virtuale | Pagine |
| --- | --- | --- | --- |
| GDT, TSS, IDT e operandi | 0 | 0x0000400000000000 | 1 |
| Guardia RSP0 | Nessuna pagina | 0x0000400000001000 | 1 non mappata |
| Stack RSP0 | 4096 | 0x0000400000002000 | 4 |
| Guardia IST1 | Nessuna pagina | 0x0000400000006000 | 1 non mappata |
| Stack IST1 | 20480 | 0x0000400000007000 | 4 |

I tre intervalli mappati sono supervisor, RW/NX. Le guardie non hanno
foglie presenti e non consumano RAM. La finestra virtuale occupa undici
pagine. NX e WP saranno effettivi con la futura attivazione CPU prevista
dal profilo; il boot corrente verifica solo le tabelle preparate.

## Allocazione e pubblicazione

`boot_allocate_descriptor_storage` verifica prima il vecchio preflight,
poi richiede nove pagine. Prima di adottarle controlla allineamento,
limite fisico CPU, profilo canonico basso, alias con owner, metadati,
risorse vive e firmware. La fotografia precedente della mappa respinge
intervalli sovrapposti a memoria diversa da ConventionalMemory, compresa
l'immagine EFI. I controlli numerici non provano da soli l'accessibilita:
questa richiede comunque un firmware conforme al contratto UEFI.

Un indirizzo ambiguo restituito dal firmware non viene adottato, azzerato
o liberato: potrebbe appartenere a un altro proprietario. Non si puo
garantire il recupero di risorse attraverso un firmware non conforme.
Una nuova allocazione valida viene registrata, azzerata e preparata;
Ready viene pubblicato solo dopo la verifica del blocco.

`boot_map_descriptor_storage` inserisce i tre intervalli in una bozza
del piano e la pubblica solo dopo la validazione. Duplicati, sovrapposizioni
e mappature nelle guardie sono respinti senza modificare il piano originale.

`boot_prepare_descriptor_handoff` invalida il contratto e ricostruisce
l'intero grafo inattivo dopo aver aggiunto tutte le risorse al piano.
Cosi l'allocatore delle tabelle protegge tutte le nove pagine, anche
quelle che non ha ancora mappato. Dopo gli ultimi rilasci e allocazioni
aggiorna GetMemoryMap, controlla grafo e copertura EfiLoaderData e
rigenera il contratto v2 con root e fotografia aggiornate.
Il preflight terminale comprende le stesse risorse quando Descriptors
e presente. Su errore le firme vengono revocate e gli owner restano
disponibili al chiamante per il cleanup.

## Rilascio e costi

`boot_release_descriptor_storage` invalida il blocco prima di FreePages.
Cancella base e conteggio solo dopo successo; un errore conserva l'owner
per un nuovo tentativo. Un owner vuoto rende il rilascio idempotente.
Questo percorso richiede servizi firmware disponibili e descrittori
ancora inattivi: invalidare la firma non scarica GDTR, IDTR o TR.

L'ingresso EFI corrente esegue il cleanup e ritorna al firmware. Se
FreePages fallisce definitivamente puo restare memoria allocata: l'owner
locale non viene conservato dopo il ritorno. La ritentabilita dell'API
non garantisce da sola recupero da ogni guasto firmware.

Il costo aggiunto e 36 KiB di dati e stack. Le nuove mappature possono
richiedere fino a tre pagine di tabelle, 12 KiB, secondo i percorsi gia
presenti. Le guardie non aggiungono pagine fisiche. Non e una misura
del picco totale del bootloader.

## Verifiche e prossimi limiti

La build esegue test di proprieta, invalidazione, rilascio ritentabile,
alias, mapping e rollback, oltre a tredici scenari dell'ingresso EFI con
guasti in allocazione, ricostruzione, fotografia e rilascio.

```powershell
& .\arch\x86\boot\uefi\scripts\build.ps1
& .\arch\x86\boot\uefi\test\firmware_test.ps1
& .\arch\x86\boot\uefi\test\handoff\descriptors\exception_firmware_test.ps1
```

Gli ingressi assembly appartengono ancora all'immagine EFI: il nuovo
grafo non ne mappa il codice. Servono codice di transizione posseduto
e mappato, trampoline, attivazione dei descrittori e del paging,
ExitBootServices reale e salto al kernel. Il probe CPU separato usa il
paging firmware; non prova questa futura sequenza. IST1 condiviso e
limitato all'arresto terminale, senza garanzie per eccezioni annidate.

Riferimenti: [servizi memoria UEFI 2.11](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel SDM volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
