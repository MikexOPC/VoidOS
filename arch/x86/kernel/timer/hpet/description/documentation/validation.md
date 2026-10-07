# R-001: descrizione HPET, primo blocco del watchdog

Scelta utente: watchdog indipendente mantenendo HLT. Questo modulo decodifica
la tabella HPET; non arma timer, non legge MMIO e non cambia il wait LAPIC.
R-001 rimane aperto. Non e ancora un driver HPET integrato nel kernel.

## Contratto e fonti

[Intel HPET 1.0a, sezione 3.2.4, Table 3](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf)
definisce layout e checksum: 56 byte, revisione 1, ID a 36, GAS a 40,
indirizzo a 44, numero a 52, minimo periodico a 53 e attributi a 55.
La regione registri occupa 1024 byte. Il minimo periodico non e il periodo
del clock e non costituisce una deadline per one-shot.

Profilo iniziale supportato: GAS SystemMemory, larghezza 0 oppure 64,
offset e quarto byte zero; indirizzo allineato a otto byte fuori da pagina
zero; intera regione entro la larghezza fisica dichiarata, 32..52 bit.
Queste restrizioni GAS/allineamento sono scelte del profilo, non una
dichiarazione che ogni altro firmware violi la specifica. Upper nibble OEM,
numero del blocco e minimo periodico sono preservati. La larghezza fisica
deve provenire dal profilo CPU verificato, non da un input utente.

C legge singoli byte e non usa struct packed per dati firmware; Rust
controlla intervalli e campi senza dereferenziare puntatori. Output pubblicato
solo dopo tutti i controlli, invariato su errore. Caller responsabile di
memoria leggibile, writable output, lifetime e assenza di mutazioni concorrenti.
Una descrizione valida non prova che il device esista o appartenga al kernel.
Il prossimo blocco deve verificare discovery, mapping, capabilities e ownership.

## Minacce e limiti

| Minaccia | Controllo | Limite |
|---|---|---|
| Tabella tronca/corrotta | Estensione esatta, header e checksum prima di decode | Checksum non autentica firmware |
| Overflow del range fisico | Rust checked_add e limite esclusivo CPU | Non prova disponibilita del mapping |
| Input/output sovrapposti | Rifiuto prima della prima lettura | Caller possiede entrambi gli span |
| Metadati riservati/non supportati | Rifiuto senza modificare output | Nessuna scrittura a registri |
| Race sul buffer | Contratto di copia posseduta e immutabile | Non accetta user pointer o buffer DMA live |

Nessuna allocazione, risorsa device o stato globale in questo componente:
leak di allocazione N/A per costruzione. ASan controlla gli accessi C host;
non misura leak di pagine fisiche e non strumenta il Rust compilato separatamente.
Nessun risultato KPTI, isolamento user o sicurezza IRQ e dedotto da questi test.

## Prove del primo blocco (2026-10-05)

Questo paragrafo conserva il risultato del decoder iniziale. Per lo stato
aggiornato del modulo, inclusa la ricerca, vedere [discovery](discovery.md).

Da root: `./arch/x86/kernel/timer/hpet/description/scripts/test.ps1`.
Lo script genera artefatti in `kernel/build/hpet-description-<id>/`, ignorati da Git.

- Due test Rust con matrici di profili validi/invalidi e limiti fisici.
- Suite C/FFI: input non allineato, campi malformati con checksum ricalcolato,
  checksum errato, lunghezze 0..55/57/SIZE_MAX, null, overlap, output non
  allineato, limite esatto, overflow e conservazione output.
- 100 ripetizioni della suite C con AddressSanitizer, warning come errori.
- Dieci avvii QEMU q35/TCG/OVMF, variabili firmware fresche per ogni avvio.
  L'app UEFI esegue decoder C/Rust e fixture, poi stampa il marker di successo.
  Questi sono test del componente in UEFI: non boot del kernel, discovery
  della tabella reale, IRQ HPET o prova di recupero da HLT.

Esecuzione 2026-10-05: tutti i controlli sopra passati; evidenze locali in
`kernel/build/hpet-description-316c187527c24a36b75b340f86cb9def/`.
Sorgenti compilati C11 /W4 /WX e Rust -D warnings. Non e coverage percentuale.
Massimo sorgente 146 righe, massimo due file diretti nelle cartelle del modulo.

Cppcheck 2.22.0: enable all, inconclusive, exhaustive, win64, C11 e library
windows sui tre file C, macro _M_X64/_MSC_VER e include root. Nessun finding
error/warning/performance; restano informazioni sugli header standard non
forniti all'analizzatore e uno style unusedFunction per efi_main. Quest'ultimo
e l'entry point passato esplicitamente al linker UEFI, non codice morto.
Non equivalgono a una prova completa di assenza bug. XML finale conservato
insieme ai log; snapshot SHA-256 dei sorgenti/test/script in source-manifest.json.
