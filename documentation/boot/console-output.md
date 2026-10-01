# Messaggi di boot e compilazione

## Formato

```text
# ============ avvio UEFI ============ #

[  OK  ] avvio UEFI riuscito.

# ============ verifica file di avvio ============ #

[ INFO ] Lettura di EFI/BOOT/boot-check.txt

[  OK  ] lettura file verificata.

# ============ validazione kernel ============ #

[ INFO ] Lettura e controllo di EFI/BOOT/kernel.elf

# ============ preparazione segmenti ELF ============ #

[  OK  ] Segmenti copiati e BSS azzerata; pagine conservate.

[  OK  ] ELF64 verificato; buffer e file rilasciati.

[ WARN ] Avvio del kernel non ancora implementato.
```

Se il file manca o e alterato, la verifica produce invece:

```text
[ FAIL ] verifica file fallita.
```

Ogni separatore occupa una riga distinta, con dodici = per lato.
Usa il colore normale. Il protocollo testuale non offre un font piu
grande per una singola riga; cambiare SetMode modifica tutta la console. Le etichette sono sempre
separate dalle altre righe di output da una riga vuota e i separatori
iniziano e terminano con `#`. Le etichette sono sempre
presenti anche su terminali senza colori. I messaggi descrivono le fasi
realmente eseguite: il bootloader non avvia ancora servizi del kernel.

## Colori e interfacce

| Livello | UEFI | PowerShell |
| --- | --- | --- |
| OK | Verde chiaro | Verde |
| INFO | Blu chiaro | Bianco |
| WARN | Giallo | Giallo |
| FAIL | Rosso chiaro | Rosso |

`console/log.c` usa SetAttribute della console UEFI, senza sequenze ANSI
scritte direttamente dal loader. Colora soltanto l'etichetta e ripristina prima del messaggio
l'attributo precedente, anche quando OutputString fallisce.
Se SetAttribute manca o restituisce un errore, prova comunque a scrivere
il testo. Se il colore e stato cambiato e il ripristino fallisce, restituisce errore
e non stampa il messaggio nel colore dell'etichetta. Il firmware puo
comunque mantenere l'attributo cambiato per il proprio output successivo.
Il colore non determina l'esito dell'operazione.
OutputString conserva il proprio stato di errore; quando fallisce una
verifica del file, il suo errore prevale sul fallimento della stampa FAIL.

`scripts/status.ps1` centralizza etichette, colori e separatori PowerShell.
La compilazione conserva le diagnostiche originali del compilatore;
i comandi falliti vengono indicati con FAIL e lo script interrompe il passo.
Anche PowerShell colora solo l'etichetta; il messaggio usa il colore normale.
I test superati sono indicati con OK solo dopo l'esecuzione.
WARN segnala il limite attuale del progetto: il kernel non viene avviato.

## Verifica

`test/log_test.c` verifica testo, livelli, colori, ripristino dell'attributo,
errori in ciascuna delle tre scritture e fallback senza SetAttribute.
I test integrati verificano la sequenza completa di separatori e messaggi.
`test/firmware_test.ps1` verifica il nuovo formato in QEMU per file corretto,
mancante e alterato, oltre agli scenari di lettura kernel descritti in
[lettura kernel](kernel-file.md), eliminando i codici ANSI aggiunti dal firmware al log
seriale prima del confronto. I test sono stati superati il 1 ottobre 2026.

I colori effettivamente visualizzati dipendono dalla console del firmware.
Dopo il ritorno del loader il firmware puo cancellare i messaggi per aprire
il proprio menu; i log seriali dei test conservano comunque l'output.

## Come provarlo

Da PowerShell nella radice `D:\OS`:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/scripts/run.ps1
```

Il primo comando compila ed esegue i test C; il secondo apre QEMU.
Per verificare automaticamente anche gli otto scenari nel firmware:

```powershell
./arch/x86/boot/uefi/test/firmware_test.ps1
```

I log restano in `build/firmware-test-<id>/<scenario>/serial.log`.

[Specifica UEFI: attributi della console](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html)




