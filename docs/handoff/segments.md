# Contratto iniziale dei segmenti del kernel

## Scopo

`arch/x86/boot/uefi/handoff/segments.h` definisce BOOT_SEGMENTS versione 1.
E il primo componente del futuro passaggio di informazioni al kernel,
non un protocollo completo di avvio. Il formato e una scelta del progetto,
non un formato imposto da UEFI o compatibile con Linux o Multiboot.

Il descrittore contiene una firma, versione, dimensione totale, entry
point virtuale, byte allocati, numero di segmenti e fino a 16 segmenti.
Ogni segmento contiene base fisica, base virtuale, byte di pagine e flag
ELF. I campi riservati e le voci inutilizzate sono zero.
Usa campi da 32 e 64 bit e il layout naturale x64: segmento 32 byte,
header 40 byte, descrittore completo 552 byte. Le dimensioni sono
controllate in compilazione e gli offset principali nei test.

## Costruzione e proprieta

`boot_describe_segments` riceve il piano prodotto dal validatore ELF e
le pagine ancora possedute dal loader. Controlla conteggi, allineamento,
dimensioni arrotondate, somme, corrispondenza delle flag e delle basi
virtuali, sovrapposizioni fisiche e virtuali e entry nei byte di file di
un segmento eseguibile. Pubblica la firma solo dopo tutti i controlli.
Su errore azzera l'intero risultato per evitare descrizioni parziali.
Input e output devono essere distinti e non modificati durante la chiamata.

La funzione non accede agli indirizzi descritti e non alloca memoria.
Non puo dimostrare da sola che un indirizzo appartenga davvero al loader:
la proprieta deriva dalle AllocatePages riuscite del chiamante.
Non sostituisce il parser del file, non applica permessi MMU e non rende
eseguibili le pagine. L'entry e virtuale: non va chiamata finche le
mappature e il contesto di ingresso non saranno stati preparati.

Nel percorso corrente il descrittore e temporaneo sullo stack, nel
[proprietario delle pagine](kernel-lifetime.md). Rimane valido durante
il controllo della mappa ed e invalidato prima del rilascio delle pagine.
Non viene passato a un kernel.
Un futuro descrittore persistente richiedera memoria propria e una
politica di durata: copiare questi dati non trasferisce la proprieta.

## Test

`test/handoff/segments_test.c` verifica layout, contenuto, azzeramento,
entry nella BSS rifiutata, conteggi errati, pagine gia rilasciate, overflow,
indirizzi non allineati, sovrapposizioni e discordanza tra piano e pagine.
Il percorso filesystem usa il costruttore dopo la copia e prima del
rilascio. Build e test si eseguono con:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

## Passi ancora necessari

La [fotografia della mappa firmware](../memory/uefi-map.md) viene ora
acquisita e controllata con GetMemoryMap in una prova separata.
Il contratto completo dovra descrivere anche le risorse
persistenti e specificare ABI di chiamata, stack, tabelle di pagina e
stato CPU. Prima di ExitBootServices serve la chiave aggiornata della
mappa. Questi passaggi non sono implementati in questa modifica.

- [UEFI: GetMemoryMap ed ExitBootServices](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
- [ELF: indirizzi e flag dei segmenti](https://gabi.xinuos.com/elf/07-pheader.html)
