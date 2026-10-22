# Dominio VT-d privato: validazione

20 ottobre 2026. [Contratto approvato e fonti](design.md).
Un dominio dedicato alla GPU, prima tabelle inattive: scelta esplicita
dell'utente. Implementazione C per ownership e prestiti RAM; Rust puro
per geometria ed entry. Nessuna pubblicazione della radice all'IOMMU.

## Cosa e implementato

Root/context legacy con un solo source ID e dominio 1, quattro livelli
second-stage, permessi R oppure R/W, riservati azzerati e nessun pass-through.
IOVA separati dai fisici, massimo 16 intervalli e 4.100 pagine DMA, guardie
e al massimo 14 pagine di strutture pinned. Il costruttore alloca solo il
numero necessario e confronta ogni entry dopo la scrittura.

Macchina a stati privata: EMPTY, ALLOCATED, BUILT_PRIVATE, VERIFIED_PRIVATE.
Il progetto include una fase di validazione prima dell'allocazione, senza
pubblicare un owner parzialmente valido. Nessuno stato ACTIVE/PUBLISHABLE.
Gli errori mantengono owner e lease quando il cleanup non e verificabile.
I buffer del chiamante devono restare pinned fino al rilascio del dominio.

Review: bounds e overflow, alias tra buffer/tabelle/paging CPU, framebuffer,
lease stantii, permessi, cleanup parziale e inclusione nel solo diagnostico.
Il cleanup ricontrolla tutti i lease prima di liberarli: una tabella
sostituita con un buffer del chiamante viene rifiutata senza consumarlo.
Nessuna nuova istruzione privilegiata, scrittura MMIO o modifica alla ABI
boot o alla dimensione dell'inventario paging, che rimane 3832 byte.

## Test host eseguiti

Runner Windows PowerShell 5.1:
`arch/x86/kernel/drivers/memory/iommu/intel/domains/scripts/test.ps1`.
Log `kernel/build/iommu-domains-host.log`; XML e binari in
`kernel/build/iommu-domains-host/`.

| Prova | Esito |
| --- | --- |
| C/ASan | 925.362 controlli; allocatore reale, lease/pinning reali salvo iniezione del fallimento pin |
| Walk indipendente C | tutti gli IOVA della finestra 32 MiB; identita bus/devfn, root/context, indirizzi fisici, permessi e guardie |
| Limiti combinati | 16 intervalli e 4.100 pagine insieme; 14 tabelle, poi cleanup completo |
| Fallimenti | ogni allocazione da 1 a 14 mediante esaurimento lease, ogni pin da 1 a 14, tutti i 28 prestiti write/read |
| Rifiuti | oltre 4.100 pagine, 17 intervalli, funzione/segmento invalidi, alias, output sull'input, permesso W senza R, framebuffer e tabelle CPU |
| Corruzioni | bit NX CPU indebito, guardia occupata, IOVA alterato, tabella sostituita con buffer, buffer non pinned |
| Rust | 2 test in debug e 2 release, 256 selettori x512 slot per root/context; limiti indirizzi, flag e geometria |
| Toolchain | MSVC C11 /W4 /WX, ASan host; Rust warning come errori e Clippy |
| cppcheck 2.22.0 | nessun error/warning/performance/portability; 7 style e 11 information conservati nell'XML |

Il walk C non usa il generatore Rust delle entry. CPU, finestra MMU e
scanner paging sono mock nel test host; i buffer e le tabelle usano
l'allocatore di produzione. Il pin delega all'implementazione reale,
salvo il singolo errore esplicitamente iniettato. Allocazioni e rilasci
sono reali anche nei test di esaurimento.

ASan senza segnalazioni nei percorsi strumentati. Accounting verificato
separatamente prima/dopo ogni caso. Valgrind non disponibile nel profilo
Windows e non eseguito; coverage linee/branch N/D.

Regressione host del mapper registri: 95.776 controlli e 800 lifecycle,
696.320 controlli indirizzi/width, Rust debug/release e cppcheck passano.
Log `kernel/build/iommu-domains-register-regression.log`.
`-TestIommuDomain -Install` rifiutato prima della build:
`kernel/build/iommu-domains-profile-negative.log`.

## Diagnostico firmware

`-TestIommuDomain` implica i diagnostici discovery e registri e resta
incompatibile con console interattiva e installazione. Due buffer RAM
diagnostici: quattro pagine R/W e 4096 pagine R, nessuna richiesta DMA.
Due cicli prepare/verify/release prima di restituire anche i buffer;
budget finale identico a quello iniziale. Non sono le code VirtIO attive.

Il diagnostico richiede una sola unita DRHD e capacita a quattro livelli;
il BDF proviene dal contratto PCI validato. Il routing DRHD definitivo non
e implementato e nessun risultato privato ne autorizza l'attivazione.

Il test QTest legge tutte le entry delle 14 tabelle ritirate, costruisce
indipendentemente gli indirizzi attesi, verifica il ramo CPU registri
revocato e GSTS=0. Durante l'uso privato il kernel esegue readback e
scanner paging reale. Non e una prova di camminata hardware IOMMU o DMA.
Il dump e successivo al rilascio, prima di qualsiasi riuso della RAM.

Nello smoke preliminare il kernel aveva completato il probe, ma il test
host aveva equiparato AGAW=48 ai bit fisici CPU. Corretto il verificatore:
il profilo espone 40 bit fisici, mentre DMAR e MGAW dichiarano 48.
Evidenza iniziale esclusa dai conteggi finali:
`kernel/build/iommu-discovery-firmware-dd62a8efcda24e0193f9d6b3ddafc1db/`.

## Campagne finali e tracciabilita

QEMU 11.1.0 build v11.1.0-12130-ge470268ff4, Q35/TCG, una CPU,
128 MiB, OVMF fornito con QEMU. VM possedute, deadline e cleanup host;
immagini copiate nella ESP privata con confronto SHA-256.

| Profilo | Boot | Evidenze sotto kernel/build/ |
| --- | --- | --- |
| GPU e VT-d, due cicli privati | 10 | iommu-discovery-firmware-5c40c109c2c64ed8b25ca6b8ab56aece |
| GPU assente, VT-d presente | 10 | iommu-discovery-firmware-813db057e98e44f78a59a6febb1e7264 |
| IOMMU assente, GPU presente | 10 | iommu-discovery-firmware-ca52219cf3fc41a38248e8f25db23f1d |
| Console ordinaria, input e cleanup | 10 | virtio-mapping-firmware-80f939c8a379497da6612597a31d6868 |
| RAM indicata come registri, rifiuto atteso | 1 | iommu-discovery-firmware-dbf802d81e9545efa29ea9c5c2781520 |
| DMAR duplicata, rifiuto atteso | 1 | iommu-discovery-firmware-b2f02bfd081c4d30b47b11767af4f3b6 |

Quaranta positivi, dieci consecutivi per profilo, nessun fault inatteso.
Due negativi raggiungono il panic previsto e mantengono il budget: sono
rifiuti della discovery/mappatura prima del costruttore, non errori DMA.
I rifiuti specifici del costruttore sono verificati nei test host.
Le due assenze non costruiscono un dominio; la console ordinaria esclude
mapper/probe registri e dominio, come verificato nel MAP della build.
Gli smoke preliminari non entrano nei conteggi finali.

Immagini finali SHA-256:

- Dominio diagnostico: `B93C996A4C41B6B2C308ACE0B34591802754E3266F84477BEACAC4D209D71FD4`.
- Console ordinaria: `A2569634847ECB0D067B5024E77CB15D346EDB057014297B8E6317DBC365BE4E`.
- Loader invariato: `C1B29183F3F3BA986DF0B5F684B8B4AE17D530AE246B015B2D629403E8489026`.

Sorgenti e adattatori: `kernel/build/iommu-domains-structure.json`.
Controllati 35 file sorgente/script; massimo 397 righe negli adattatori,
cinque sorgenti diretti nel modulo. Include, link, LF, trailing whitespace
e diff finale verificati. Nessuna modifica a reuse/, installazione o push.

## Limiti e prossimo gate

ECAP.C=0 rimane un requisito da gestire: le tabelle private non vengono
pulite/pubblicate per il page walk hardware. Non cambia `Compatible=0`
del lettore registri. Mancano cache maintenance, routing definitivo,
protocollo comandi, invalidazioni, gestione fault e revoca dopo DMA reale.
Prima di consentire letture DMA, il proprietario deve anche inizializzare
i dati che espone; l'allocazione privata non inizializza i payload.

Nessuna console VirtIO persistente, ACCESS_PLATFORM o protezione DMA
attiva dichiarata. Niente hardware fisico, SMP, PASID, ATS o AMD-Vi provati.
