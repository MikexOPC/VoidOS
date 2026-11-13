# Snapshot PCI: contratto del blocco topologia

22 ottobre2026. Attua la parte topologica della [scelta B gia approvata](../../../../../../../../docs/design/consolidamento.md#video-vt-d-e-ps2).
Prerequisito: [lettore nativo ECAM verificato](../../ecam/documentation/validation.md).
Il resolver multi-DRHD e l'attivazione DMA restano blocchi distinti.

## Fonti

[Intel316966-002](https://www.intel.com/Assets/PDF/datasheet/316966.pdf),
tabelle5-1/6-1 e §§6.1.9/10/11, pagine188/189: campi standard PCI,
primary0x18, secondary0x19, subordinate0x1a. Header type al byte0x0e,
bit7 multifunction e bit6:0 tipo; il DWORD a0x0c li comprende. ID a0,
classe/revisione a8. Le letture usano esclusivamente gli offset ECAM
gia autorizzati; niente config write, BAR probing o renumbering.

[QEMU v11.1.0 pci_bridge.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/pci/pci_bridge.c)
e [schema QMP pci.json](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/qapi/pci.json)
forniscono il confronto indipendente del modello e della gerarchia.
Nessun codice dei progetti di riferimento copiato.

## Acquisizione e limiti

MCFG definisce finestre, non radici topologiche. Si percorrono tutti i bus
configurati in ciascuna finestra, senza assumere una sola radice o bus0.
Per ogni device si legge function0; vendor0xffff indica assenza. Se il
bit multifunction e impostato si leggono anche function1..7. E il profilo
PCI tradizionale: ARI/SR-IOV/requester alias non sono certificati da questa
enumerazione. Vendor0/1 e header non supportati producono un rifiuto,
senza retry illimitati o pubblicazione di informazioni incerte.

Massimo512 funzioni,64 bridge, profondita8,32 finestre/8 segmenti ereditati
da MCFG,150000 letture scalari per cattura. I limiti sono del profilo,
non dell'architettura PCI universale. Nessuna truncation: capacita o
profondita superate falliscono. La scansione esaustiva ha un costo da
misurare; non si memorizzano autorizzazioni saltando i controlli ECAM.

Passo1 costruisce i nodi privati. Il grafo viene derivato e validato.
Passo2 ripete l'enumerazione, inclusi gli slot assenti, e confronta BDF,
ID, classe, header e bus bridge. Un device nuovo, rimosso o cambiato
invalida la cattura. Anche MCFG viene ricontrollata contro la copia usata.
Due passaggi uguali non provano un'istantanea atomica o assenza di una
mutazione ABA: il primo profilo diagnostico non supporta hotplug attivo.

## Grafo e invarianti Rust

Ogni nodo32 byte contiene identita, registri necessari e Parent/Depth.
Parent=0xffffffff indica radice. BDF unici nel segmento; secondary bus
appartiene a un solo bridge attivo. Il parent di una funzione e il bridge
il cui secondary corrisponde al bus della funzione, non un intervallo
scelto per vicinanza numerica. Gli intervalli subordinate dei bridge
fratelli non si sovrappongono; quelli dei discendenti restano contenuti
negli antenati. Un bus rivendicato da un intervallo ma senza un percorso
di parent verificabile viene rifiutato. Cicli, parent esterni e profondita
oltre8 falliscono. Ordine dei nodi e numeri di bus non definiscono il parent.

Bridge type1 richiedono classe0604. Un bridge con secondary/subordinate
zero viene registrato come disabilitato e non rivendica bus downstream.
Un endpoint non porta campi bridge. Una funzione secondaria richiede
function0 con bit multifunction nello stesso device. Snapshot di segmenti
diversi non possono diventare antenati gli uni degli altri.

Rust puro senza heap gestisce il grafo, con slice e accessi controllati.
Solo la sottile FFI usa unsafe per creare slice dai buffer C: count<=512,
allineamento e overflow verificati; mappatura, capacita e lifetime restano
obblighi del chiamante C. La slice non sfugge alla chiamata. Lo staging
puo essere mutato anche in un rifiuto; il risultato pubblico rimane invariato.

## Ownership C e pubblicazione

Il chiamante fornisce workspace e output distinti,16936 byte ciascuno.
Non sono oggetti sul piccolo stack kernel: il diagnostico usa storage
statico. Nessun heap Rust, lease o allocazione fisica aggiuntiva. Il wrapper
nativo rifiuta sovrapposizioni con reader, contesto, pool, window, inventario,
LAPIC e memory map; il core protegge inoltre source e aperture speciali.
Il provider generico deve rispettare lifetime e non mutare workspace/output.

Tutte le letture passano dal reader ECAM e dal suo borrower, con verifiche
e cleanup per ciascuna transazione. Il provider viene usato solo durante
capture. Firma pubblica assegnata soltanto dopo due passaggi, controllo
grafo, source e coda azzerata; copia nell'output soltanto al termine.
La firma non e un token DMA o una capability valida dopo cambi hardware.

Sequenza non eseguibile:

    EMPTY -> PRIVATE_SCAN -> GRAPH_VERIFIED -> SECOND_SCAN_MATCHED
          -> SNAPSHOT_VERIFIED -> OUTPUT_PUBLISHED

Fallimento: output precedente immutato. Revoca reader incerta: nessuna
pubblicazione, il proprietario del reader mantiene il suo stato e gestisce
close/recovery secondo il contratto ECAM, senza liberazioni alla cieca.

## Accettazione

Host C/ASan: errore a ogni lettura del riferimento, rollback logico,
cambi e nuove presenze tra passaggi, alias, capacita e validazione output.
Rust debug/release e Clippy: grafi annidati/disordinati, cicli, overlap,
multifunction, piu radici/segmenti, massimi e corruzione Parent/Depth.
Cppcheck, warning come errori, nessuna coverage inventata.

QEMU: almeno10 boot consecutivi per profilo positivo finale, GPU radice,
root port e switch PCIe annidato. Snapshot binario letto traducendo il VA
tramite CR3, tutti i nodi e parent confrontati col grafo QMP e registri
CF8/CFC della VM ferma. Regressioni ECAM, QI e console ordinaria; hash ESP,
budget e window revocata. Hardware fisico/SMP/hotplug restano non provati.
