# Preparazione del contratto e dello stack ABI

`handoff/protocol/prepare.c` produce il contratto numerico dalle tabelle
ancora possedute e dal piano verificato delle mappature. Non alloca pagine,
non scrive registri e non trasferisce la proprieta delle risorse.

## Dati preparatori

La preparazione controlla il profilo CPU, i limiti fisici, root e lista
delle pagine delle tabelle, duplicati e sovrapposizioni con kernel o stack.
Controlla il piano e pubblica la firma soltanto dopo tutti i controlli.
Versione, dimensione, ingresso e stack derivano dal profilo corrente.
`EnabledFeatures` resta zero: costruire tabelle non abilita NXE o CR0.WP.
Il probe rifiuta quindi questa bozza se usata come contratto attivo.

Il chiamante fornisce oggetti validi, con output distinto dagli input,
che restano vivi durante la preparazione. L'output non possiede pagine.
Su errore il contratto viene azzerato; la funzione di invalidazione
azzera tutti i campi senza liberare le risorse da cui erano derivati.
Il boot ora prepara la bozza in una [pagina dedicata](start-page.md),
la mappa nelle tabelle inattive e la invalida prima del rilascio.

Questi controlli non dimostrano che i contenuti delle tabelle corrispondano
al piano e non autorizzano il cambio CR3. La copertura EfiLoaderData e
verificata separatamente nella fase precedente. Una futura pubblicazione
richiede memoria dedicata mappata e verifica dello stato effettivamente
attivato, oltre alle risorse della transizione.

## Stack della futura CALL

Il calcolo accetta intervalli virtuali bassi, allineati a pagina e senza
overflow. Non scrive sullo stack. Con lo stack attuale di 64 KiB:

- RSP prima della CALL: fine stack meno 32 byte, allineato a 16.
- RSP all'ingresso: fine stack meno 40 byte, dopo il push dell'indirizzo di ritorno.
- Shadow space: 32 byte sopra l'indirizzo di ritorno.

Il callee riceve RSP con resto 8 modulo 16. Un futuro trampoline dovra
impostare realmente RSP, RCX e l'indirizzo di ritorno valido; questo
calcolo non effettua la chiamata e non dimostra che lo stack sia attivo.

## Verifiche

La build testa bozza valida, diciassette variazioni incompatibili, azzeramento
di tutti i 72 byte su errore,
alias fisici delle tabelle con kernel e stack, limiti CPU e sei intervalli
di stack non validi. QEMU controlla l'ordine della nuova fase prima del
rilascio, mantenendo invariato CR3 in entrambi gli scenari positivi.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

[Convenzione Microsoft x64](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention):
shadow space, allineamento dello stack e argomento in RCX.
