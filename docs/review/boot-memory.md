# Revisione del boot del 2 ottobre 2026

Per la verifica successiva dello stato completo e gli hash delle nuove
esecuzioni, vedere [validazione del consolidamento](../validation/consolidamento.md#boot-memoria-e-dispositivi).

## Ambito

Controllati allocazione dei segmenti ELF, stack, pagina del contratto,
mappa persistente, tabelle inattive e percorso terminale UEFI.
Il confronto con la documentazione ufficiale conferma l'uso di
EfiLoaderData, della chiave aggiornata e di servizi limitati dopo il
primo tentativo di ExitBootServices. La chiamata richiede TPL_APPLICATION.

## Difetti corretti

- Lo stack poteva accettare un'allocazione sovrapposta ai segmenti del
  kernel, ai proprietari o alla tabella dei servizi prima di azzerarla.
  Ora riceve le riserve del kernel, ne valida conteggi, dimensioni e
  budget, poi rifiuta gli alias prima di adottare o scrivere le pagine.
- La pagina del contratto proteggeva le risorse del piano, ma mancavano
  i controlli sui metadati del piano, del proprietario e dei servizi.
- Il caricamento ELF non proteggeva la tabella dei servizi da un
  risultato AllocatePages sovrapposto; ora controlla anche HeaderSize.
- Lo stato terminale poteva sovrapporsi a risorse o proprietari presi
  in prestito. Ora viene rifiutato prima delle scritture e delle chiamate.

Questi output di allocazione segnalano firmware non conforme: non vengono
adottati, azzerati o liberati come nuove risorse. Non si puo liberare una
zona ambigua senza rischiare di danneggiare un proprietario gia vivo.
I controlli numerici non certificano l'accessibilita di puntatori arbitrari.

## Struttura e verifiche

C e header restano nella cartella funzionale del componente, mentre
Rust usa `rust/<funzione>/`, anche nei test. Il bridge delle prove di
paging e `test/paging/bridge.c`, le prove Rust in `test/paging/rust/allocation/`.
Aggiornati build, include, import Rust e documentazione.

Build MSVC con /W4 /WX e Rust con warnings vietati superata. Passati i
test C/C++, le 5000 mutazioni ELF, i test numerici Rust e i sette test Rust
delle tabelle C, incluse 512 sequenze di errore riproducibili. Le nuove
regressioni coprono alias completi e parziali, riserve kernel invalide e
conservazione dei byte quando lo stato terminale invade risorse vive.
QEMU con EDK2 ha superato tutti i dieci scenari del progetto.

L'immagine EFI occupa 54272 byte. Le guardie non aggiungono allocazioni
al boot; i sorgenti Rust spostati mantengono le stesse interfacce.

## Limiti verificati

Il boot reale prepara e controlla le tabelle, poi libera le risorse e
ritorna al firmware. Non attiva CR3, NXE o WP e non esegue il kernel.
ExitBootServices e verificato con mock host; QEMU esegue il preflight.
Non sono ancora disponibili trampoline, gestione autonoma delle eccezioni,
autenticazione del kernel o prove su hardware fisico. Questa revisione
non dimostra l'assenza di ogni difetto.

## Fonti ufficiali

- [UEFI 2.11: allocazioni, GetMemoryMap, ExitBootServices e TPL](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
- [Intel SDM volume 3A: paging e registri di controllo](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf)
