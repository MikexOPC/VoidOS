# Primo avvio UEFI

Snapshot della prima base loader. Stato corrente: [docs/status.md](../status.md).
Il percorso attuale carica il kernel, prepara paging e termina i Boot
Services; questo file descrive il primo passo, che tornava al firmware.

Questa e la prima base del bootloader: un'applicazione UEFI x64 in C.
Mostra `[  OK  ] avvio UEFI riuscito.`, verifica il file di prova e
`kernel.elf` sul volume di avvio e ritorna al firmware. Il messaggio
puo scomparire quando il firmware riprende il controllo.
Legge il kernel in un buffer temporaneo, prepara i segmenti in pagine
allocate dal firmware e li rilascia. In quella base non termina i Boot
Services.

## Struttura

Il codice specifico x86 segue la struttura `arch/x86/`.
Il bootloader UEFI si trova in `arch/x86/boot/uefi/`; i suoi test sono
separati in `test/arch/x86/boot/uefi/entry_test.c`.
`file_probe.c` gestisce la lettura, `file_protocol.h` contiene solo i
prefissi delle interfacce UEFI necessari; `test/file_test.c` li simula.
`storage/kernel_file.c` legge il kernel completo e applica il validatore
ELF. Contratto e memoria sono descritti in [lettura kernel](kernel-file.md).
Gli script si trovano in `arch/x86/boot/uefi/scripts/`; gli artefatti
generati restano in `arch/x86/boot/uefi/build/`.
La documentazione segue `documentation/<funzione>/`.
`kernel/`, `mm/` e `lib/` esistono; non attendono una fase successiva.

## Compilazione e test

Da PowerShell nella radice del progetto:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
```

Serve il toolset C x64 di Visual Studio Build Tools 2022. Se installato
altrove, passare `-ToolchainBin` con la cartella di `cl.exe` e `link.exe`.
Non servono CRT, Windows SDK o assembly. I tipi UEFI sono un sottoinsieme
locale della specifica; i puntatori ai servizi non usati rimangono opachi.
La convenzione Microsoft x64 e obbligatoria anche per le callback.

Il risultato e `arch/x86/boot/uefi/build/esp/EFI/BOOT/BOOTX64.EFI`.
Lo script verifica layout ABI, messaggio UTF-16, ritorno della console,
parametri mancanti e header invalido. Controlla inoltre PE32+, macchina
x64, sottosistema EFI, entry point, assenza di import e rilocazioni.
I test sono ospitati in una DLL Windows: non dimostrano l'avvio UEFI.
Ogni esecuzione genera una DLL con nome distinto per evitare i blocchi
dei file caricati nella sessione PowerShell; restano nella cartella build.

## Lettura dal volume di avvio

Lo script crea `EFI/BOOT/boot-check.txt` con i 15 byte ASCII
`OS boot check` seguiti da CR e LF. Il loader usa Loaded Image e il
suo DeviceHandle per accedere al filesystem del volume da cui proviene.
Apre volume e file in sola lettura e verifica il contenuto esatto.
Il buffer sullo stack e di 64 byte; non usa allocazioni dinamiche.
Gestisce letture parziali fino alla fine del file o al limite di 64 byte.
Contenuti diversi, file vuoti o troppo grandi vengono rifiutati.

In caso di successo stampa `[  OK  ] lettura file verificata.`; in caso di
errore stampa `[ FAIL ] verifica file fallita.` e restituisce lo stato UEFI.
Separatori e colori sono descritti in [output console](console-output.md).
Chiude file e volume aperti anche quando la lettura fallisce. Il primo
errore prevale sugli errori di chiusura successivi. Le interfacce del
firmware devono essere conformi: un protocollo privo di Close non puo
essere rilasciato dal loader, che segnala EFI_UNSUPPORTED.
I test verificano anche le posizioni ABI dei nuovi campi, letture parziali,
file mancante, contenuti invalidi, limite del buffer, errori di chiusura
e il percorso integrato dal punto di ingresso.

## Prova nel firmware

Con QEMU Windows installato in `C:\Program Files\qemu`:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/scripts/run.ps1
```

`run.ps1` usa il firmware EDK2 incluso, con CODE in sola lettura e
una copia di VARS in build. Espone soltanto la cartella esp generata,
usa emulazione software TCG, 128 MiB di RAM e nessuna rete.
Per un'installazione diversa passare `-QemuDirectory`.
Non serve aggiungere QEMU al PATH.

Occorrono QEMU x64 e un firmware OVMF x64 compatibile, forniti separatamente.
Esempio con un'immagine firmware monolitica per prove temporanee:

```powershell
qemu-system-x86_64 -machine q35 -m 128 -bios "C:/percorso/OVMF.fd" -drive "format=raw,file=fat:rw:arch/x86/boot/uefi/build/esp" -net none
```

I pacchetti che separano CODE e VARS richiedono invece la configurazione
pflash appropriata e una copia scrivibile di VARS; non usare CODE da solo
come se fosse un firmware monolitico. L'immagine iniziale non e firmata:
usare un ambiente di test senza Secure Boot attivo.
Verificare entrambi i messaggi di successo e il ritorno al firmware.
Rinominare temporaneamente il file di prova deve produrre il messaggio
di errore. La prova positiva e stata eseguita il 1 ottobre 2026 con
QEMU Windows 11.1.0 e il firmware EDK2 incluso: entrambi i messaggi
sono presenti nel log seriale, seguiti dal ritorno al menu del firmware.
Log locale: `arch/x86/boot/uefi/build/uefi-test-serial.log`.
Anche le prove di file mancante e alterato sono state eseguite e superate
il 1 ottobre 2026. Il firmware riceve rispettivamente Not Found e Load Error.
Nei casi di errore puo avviare la shell UEFI invece del menu.

Per ripetere automaticamente gli otto scenari di filesystem e kernel:

```powershell
./test/arch/x86/boot/uefi/firmware_test.ps1
```

Il test usa copie separate dell'eseguibile, del volume e delle variabili
firmware in `build/firmware-test-<id>/`. Non altera il volume usato da run.ps1.
Avvia QEMU senza finestra, verifica messaggi e ritorno al firmware con
lo stato atteso, poi termina solo il proprio processo QEMU. Il limite
predefinito e 35 secondi per scenario, regolabile con `-TimeoutSeconds`.
I log seriali e stderr di ogni scenario restano nella relativa cartella.
Il riconoscimento del ritorno dipende dai messaggi BdsDxe del firmware EDK2
incluso nel pacchetto verificato; altri firmware potrebbero richiedere
di adattare i pattern del test.
Non modificare disco,
partizioni o impostazioni di avvio del PC per questa prima prova.

## Limiti e passo successivo

Il loader prepara uno [stack dedicato e verifica la copertura](../handoff/resources.md)
di kernel e stack nella mappa UEFI prima del rilascio. Restano
la paginazione, il contratto completo di ingresso e il salto al kernel.

Il loader verifica anche una [fotografia della mappa UEFI](../memory/uefi-map.md)
con buffer temporaneo e rilascio esplicito. Non conserva questa chiave
per ExitBootServices. Una [classificazione conservativa](../memory/regions.md)
distingue le pagine convenzionali candidate da quelle escluse.

La prova filesystem usa un buffer da 64 byte sullo stack. La lettura
kernel usa un buffer temporaneo allocato di massimo 64 MiB, controllando
la dimensione prima dell'allocazione e rilasciandolo dopo la validazione.
Il controllo dell'header non e una verifica CRC o di sicurezza del firmware.
Per ora i file sono piccoli per mantenere il passo minimo concordato.
Il file di prova non e un kernel. Il formato iniziale scelto per i controlli
e ELF64 statico x86-64; mappature e passaggio al kernel restano da costruire.
Il primo profilo ELF64 x86-64 e ora implementato come modulo separato:
vedere [validazione ELF](elf-validation.md) per contratto, limiti e test.
La build ora genera un [kernel C diagnostico](../handoff/kernel-probe.md)
in formato ELF reale. QEMU verifica sia questo probe sia la fixture
sintetica di 512 byte mantenuta per i test. La preparazione dei segmenti e implementata e descritta in
[preparazione segmenti](segment-loading.md). Non viene eseguito codice del kernel.

## Riferimenti

Il piano delle mappature e ora controllato nel boot. Il costruttore delle
tabelle inattive e verificato separatamente: vedere [paging](../paging/README.md)
per permessi, proprieta e limiti dell'integrazione.

- [UEFI: tabella di sistema e punto di ingresso](https://uefi.org/specs/UEFI/2.9_A/04_EFI_System_Table.html)
- [UEFI: console di testo](https://uefi.org/specs/UEFI/2.9_A/12_Protocols_Console_Support.html)
- [UEFI: boot manager e nome BOOTX64.EFI](https://uefi.org/specs/UEFI/2.10_A/03_Boot_Manager.html)
- [Specifica UEFI 2.11 completa](https://uefi.org/sites/default/files/resources/UEFI_Spec_Final_2.11.pdf)
- [UEFI: Loaded Image](https://uefi.org/specs/UEFI/2.11/09_Protocols_EFI_Loaded_Image.html)
- [UEFI: filesystem e file](https://uefi.org/specs/UEFI/2.11/13_Protocols_Media_Access.html)


