# Checkpoint descrittori coerenti del 2 ottobre 2026

Tre funzioni pubbliche: preparazione transazionale, verifica incrociata
e invalidazione del preflight. Un blocco da 704 byte contiene GDT estesa,
TSS, IDT e operandi. Nessuna nuova allocazione del boot ordinario.
Il probe ora usa le funzioni di produzione invece di modificare a mano
GDTR e gate. L'invalidazione della firma precede il caricamento CPU e
non modifica i descrittori.

Ciclo iniziale: implementazione e test. Corrette la dipendenza da stddef.h
assente nella toolchain freestanding e la disposizione esplicita dei
campi riservati, senza silenziare warning. Secondo ciclo: consumo C++,
test agli estremi canonici, controllo delle regioni adiacenti e revisione.
Corretta un'attesa del test che rifiutava due intervalli adiacenti validi.

Build finale: C/C++ /W4 /WX, assembly /W3 /WX, Rust con warnings vietati,
suite precedente e nuova suite superate. Mutazioni: 5.632 bit del blocco,
inclusi i campi riservati, e 2.240 bit del profilo; 7.872 in totale.
Alias e argomenti invalidi conservano l'output. Testata l'invalidazione
della sola firma e il rifiuto degli ingressi dentro descrittori o stack.

QEMU/EDK2: 10/10 scenari ordinari sulla stessa EFI; 3/3 eccezioni reali
nel probe, con LTR, bit busy, stack IST1 e arresto verificati. #GP conserva
il codice d'errore 0x28. Immagine ordinaria: 59.392 byte, dimensione invariata.
Il nuovo componente viene collegato nei test e nel probe, non nel percorso
EFI ordinario ancora privo della risorsa persistente.

| Artefatto in arch/x86/boot/uefi/build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 0B739C31EB989147F685F4F707E848E30708D55031B558AADCE9E2D50BCDF749 |
| bundle-build.log | B8EAA36DD84B29F55AD029F566F3F3C2E68331AF064F0DD4A15E4E3340B08B41 |
| bundle-probe.log | 7C97EE4F0D5E7FFCC224482AC684488EBD8CAB0B4832A82C64DE35DA7A1F7762 |
| bundle-firmware.log | 4E06757F184D0AEF40E483E32B9FC0DC13EB5E9A7AE083B01DEE11825792504E |

Log CPU: exception-test-fc952eef9de24779b8a0a032a6db7120.
Log ordinari: firmware-test-eee7aa3154224e13ba1f4042cad8658e.
Artefatti grezzi locali in build/, ignorati da Git.

Stime di lavoro rimanente ancora circa 40-45% per l'ingresso in un kernel
minimale QEMU e 65-75% per un loader iniziale affidabile su hardware reale
x86_64 UEFI selezionato. Sono stime di maturita, non percentuali di righe
o tempi. I passi principali ancora aperti sono risorse persistenti,
guard page e mappature della transizione, trampoline, uscita UEFI reale,
protezioni attive e salto al kernel; poi test su macchine fisiche.
Il progresso di questo blocco riguarda la coerenza e la verifica dei
descrittori, senza dimostrare quei passaggi finali.

Il profilo IST1 comune serve solo all'arresto terminale del probe e non
gestisce eccezioni annidate. Contratto kernel v2 e obiettivo kernel ibrido
invariati. [Proprieta e limiti del componente](../handoff/descriptors/transition.md).
