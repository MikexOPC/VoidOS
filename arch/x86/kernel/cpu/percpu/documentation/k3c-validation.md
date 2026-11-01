# K3-C — validazione del registro CPU di boot

29 ottobre 2026. Scelta approvata: registro stabile al boot, hotplug dopo.
[Contratto, fonti e limiti](k3c-design.md).

## Implementazione e review

Il registro associa tre identità distinte: indice logico, APIC ID e UID
ACPI. Il BSP occupa l’indice zero dopo matching esatto con il profilo APIC
verificato; gli AP candidati seguono l’ordine dei record abilitati della
MADT, senza assumere ID consecutivi. Nessun core viene avviato.

Rust valida campi, flags, larghezze e distinzione UID/ID tramite funzioni
scalari senza dereferenziazioni o blocchi unsafe. C possiede lo snapshot,
verifica capacità/range/alias, prepara una bozza e pubblica solo il piano
completo. La cattura nativa richiede MADT e profilo APIC validi, contesto
GS BSP quiescente e storage ancora vuoto. Doppia inizializzazione rifiutata,
anche se un campo Ready fosse azzerato senza ritirare l’owner.

Il getter è BSP-only e preserva IF; verifica Self, contesto GS, conteggi,
indici, duplicati e coda inutilizzata zero. Input copiati, nessun prestito
alla MADT originale. ABI con static_assert: record 16 byte, prefisso 32,
snapshot 1.056 byte in BSS, piano temporaneo 1.056 byte sullo stack.
Nessuna allocazione dinamica o nuova risorsa fisica da rilasciare.

Mutazioni a IF=0 sono una garanzia locale BSP, non sincronizzazione SMP.
Ready non costituisce un protocollo atomico per futuri AP. I confronti
sono O(n²), con n massimo 64 e 2.016 coppie per scansione; nessun array
indicizzato dalla grandezza dell’APIC ID.

## Host finale

Nuova suite percpu-identity:
`kernel/build/test-run-6f81d88ccd014e9594ffed57cd4b24a7/percpu-identity.log`.

- **2 test Rust**, entrambi passati in debug e release (4 esecuzioni).
  Flags riservati, UID/ID larghi, ID sparsi, identità duplicate e limiti
  xAPIC, senza introdurre una conversione implicita a x2APIC.
- **20 esecuzioni C ASan**: dieci ripetizioni IF0/IF1. Ogni esecuzione
  include tutti i 64 possibili posizionamenti del BSP in un elenco di
  64 ID sparsi, capacità 64/65, input nullo, output in alias, duplicati,
  BSP assente/non abilitato, offline/ignorati e output conservato su errore.
- Provider GS e lock reali. Controlli nativi MADT/profilo modellati nel
  test host, distinti dall’integrazione effettiva verificata in QEMU.
- Pubblicazione una sola volta, rollback prima della pubblicazione,
  snapshot indipendente dagli input, Depth non zero e AP non ammessi,
  corruzione dei conteggi/identità/coda/contesto rifiutata dal getter.
- MSVC /W4 /WX, C ASan, Rust con warning come errori. Analisi statica di
  tutti i quattro nuovi C: `kernel/build/k3c-static/cppcheck.xml`, nessun
  error/warning/style/performance/portability; dieci informazioni sugli
  header di sistema non risolti da cppcheck. MSVC usa gli header effettivi.

Regressioni host passate:

| Suite | Evidenza in kernel/build/ |
|---|---|
| percpu-clock | test-run-21056bb6a13641369e0f6f60529af04b |
| percpu | test-run-8acb158cfa21472ca25dd9c756f47e8a |
| bootstrap | test-run-e4be5524070440a6827474067513d35f |

ASan copre il C host strumentato; Rust non è strumentato con ASan in questa
suite. Valgrind non eseguito su Windows. Nessuna affermazione di assenza
di leak dell’intero kernel o copertura linee/branch, che rimane N/D.

## QEMU finale

QEMU 11.1.0, q35/TCG/OVMF, 128/256 MiB, alternanza 1/4 vCPU configurate.
La configurazione non viene confusa con CPU avviate: **un solo contesto OS
pronto, il BSP**, in tutte le prove. BSP non primo/ID non zero e ID sparsi
sono collaudati sull’host; le campagne QEMU standard hanno BSP APIC ID zero.

| Profilo | Risultato | Evidenza in kernel/build/ |
|---|---|---|
| Diagnostico con sync probe | 10/10 consecutivi | percpu-firmware-b3606ad6fa18453f964adf6160fb68cd/results.json |
| Ordinario | 10/10 consecutivi | percpu-firmware-b271326e2f83458c931daa6c52b35496/results.json |
| Negativi lock | 2/2 attesi | qemu-run-5da9d43e7be641a1924075ebdbf3b9f9/results.json |
| Console VirtIO/VT-d | 1/1 PASS | qemu-run-788115aa8c1a445387a2b79159247eaf/results.json |

Il monitor legge GS, contesto e descrittore PM come in K3-A/B. In più
legge lo snapshot effettivo, il prefisso e i record CPU della MADT persistente:
ricostruisce il matching BSP da ID/UID del profilo, confronta ogni record
con l’ordine logico previsto, conteggi enabled/offline/ignored, ABI e coda
zero. La struttura non viene verificata solo tramite le stampe del modulo.
I runner verificano copie ESP/hash, marker e assenza di fault inattesi,
con deadline e cleanup della propria VM. Nessuna esecuzione parallela
tra le VM di questa campagna.

Il diagnostico usa `kernel/build/k3c-first/kernel.elf`: il nome first
indica la cartella iniziale; i sorgenti eseguibili non sono cambiati dopo
questa compilazione. Il manifest finale ne conserva gli hash.

SHA-256 ELF diagnostico:
`65334D00843BA1088113187BE0CBC4B770140284C423B55E52EBBE1017346BFF`.
SHA-256 ELF ordinario:
`CC32111E93DE9A191FEBEB464EE5E360D46A2C14C07954516A7B9438A20A07A2`.
Loader comune:
`B6C055AB17CC5CA1E0F017A35E4E9722273EB7B49CB0191F5E759CA84CC23AA1`.

La regressione video conserva input, movimento, screenshot/pixel e cleanup
in `kernel/build/pointer-interactive-check-b268b50fc63e4c7f841541b8746f339a/`.
È una sessione completa, non una campagna da dieci sessioni video.

## Compatibilità e lavoro residuo

Il pianificatore puro sa ignorare Flags=0 secondo ACPI, ma il parser MADT
preesistente rifiuta quei record prima che arrivino al registro. Tale
parser non è stato modificato: non dichiarare nuova compatibilità nativa
con quei firmware. Online Capable viene contato separatamente, senza
accensione o hotplug. I record type 9 mantengono UID/ID a 32 bit, ma il
profilo corrente resta xAPIC e rifiuta ID abilitati non indirizzabili.

Nessuna modifica ad allocatore, heap, scheduler, watchdog, controller APIC,
GDT/TSS/IST o vecchio OS. Nuovi sorgenti sotto 400 righe e cinque file
diretti; manifest: `kernel/build/k3c-source-review.json`.

K3 non è chiuso: seguono ownership e mapping degli stack con guardie,
GDT/TSS/IST privati, risorse/clock per AP, trampolino e ingresso, avvio e
pubblicazione dei core pronti, quindi dieci boot con quattro CPU realmente
operative. Nessuna prova di SMP o hardware fisico in questo blocco.
