# Stato verificato il 2 ottobre 2026

Snapshot storico del solo bootloader, prima del salto al kernel.
Lo stato corrente e in [docs/status.md](../status.md). Le frasi al
presente qui sotto descrivono quel checkpoint, non il kernel attuale.

Questa verifica riguarda lo stato completo preparato per il secondo commit,
incluse modifiche precedentemente staged, modifiche locali e spostamenti.
I vecchi log di build non costituiscono evidenza per questo stato.

## Riproduzione

Dalla radice del progetto, eseguire in sequenza:

```powershell
& .\arch\x86\boot\uefi\scripts\build.ps1
& .\test\arch\x86\boot\uefi\firmware_test.ps1
```

La build corrente ha superato i test C/C++, le 5000 mutazioni ELF,
i test numerici Rust e i sette test Rust delle tabelle C, comprese
512 sequenze di errore. L'immagine EFI verificata misura 54272 byte.

QEMU/EDK2 ha superato 10/10 scenari: valid, kernel-probe, missing,
altered, kernel-missing, kernel-truncated, kernel-oversized,
kernel-bad-segment, kernel-bad-magic e cpu-no-nx.
Lo script confronta SHA256 di ogni copia EFI con l'immagine iniziale
e controlla che questa resti identica fino al termine dei test.
I sorgenti e gli script nel manifest sono rimasti identici durante QEMU.

## Evidenze locali

Gli artefatti restano nella cartella ignorata `arch/x86/boot/uefi/build/`.
Questo documento conserva esiti e hash; i log grezzi non sono nel commit.

| Artefatto relativo a build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | FF6EB0D78AF37328394FE015171260577DB225E4CC23BC29996AF933C1AC10C1 |
| latest-build.log | E383154CC1B7F12749BCB401DF562A6F7487A0B106AE14832782A71D091A2316 |
| latest-firmware.log | B95CA53E59684F1CFBA17266D45B04FB745E9B0CE09623068062E849CEC0E052 |
| verified-sources.sha256 | 1F558FABA598A5FB23167AD8E29F6F63B722571C01577A085B2F6350898514E5 |

Log seriali: `firmware-test-9966cce985644e119bb26c4b8c259b44/<scenario>/serial.log`.
Nel nuovo scenario valid, CR3 prima e dopo vale `0x0000000007801000`;
la root inattiva vale `0x00000000065B9000`. Seguono contratto v2,
mappa persistente R/NX, preflight di uscita e rilascio delle pagine.
Questi indirizzi sono risultati della singola esecuzione, non valori fissi.

## Decisioni e limiti

- `.gitattributes` impone LF ai file di testo. Normalizzati 14 file;
  il controllo iniziale non ha trovato CR isolati nei testi esaminati.
  `entry.c` contiene ora zero byte CR; `git diff --check` passa.
- Nell'allocatore delle tabelle, un alias restituito da firmware non
  conforme resta di proprieta ambigua e puo comportare perdita di risorse.
  Non viene liberato: FreePages potrebbe danneggiare un proprietario vivo.
  La scelta e ora esplicita nei commenti del codice.
- L'output distinto viene registrato prima della verifica numerica per
  consentire il tentativo di cleanup senza dereferenziarlo. La regressione
  esistente per il limite fisico CPU verifica il rilascio di questa pagina.
- La sequenza esplicita di entry.c resta invariata in questo checkpoint.
  Prima di aggiungere il salto al kernel, va rivalutata la separazione
  delle fasi e del cleanup; una tabella generica non esprime da sola
  proprieta delle risorse e transizioni irreversibili.
- In questo checkpoint il kernel non veniva eseguito. ExitBootServices
  resta provato con mock host, mentre QEMU verifica il preflight. CR3
  non cambia e NXE/WP non vengono attivati dal bootloader. Mancano
  trampoline e prove fisiche. Quel passaggio e stato affrontato in
  blocchi successivi; i test host di questa data non lo dimostrano.

Riferimento: [UEFI 2.11, AllocatePages e FreePages](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).
