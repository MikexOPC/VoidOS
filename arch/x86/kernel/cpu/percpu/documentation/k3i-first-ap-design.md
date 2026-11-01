# K3-I — adattatore del primo AP

Continuazione del diagnostico AP e del protocollo NMI già approvati.
Un solo AP, indice 1 del gruppo immutabile; nessun cambio al boot ordinario,
nessun riuso dei dati bassi e nessuna liberazione dopo le IPI.

Prima dell'avvio: configure/reserve Rust, record diretto al prefisso vero,
installazione, commit congiunto, panic, snapshot e chiusura dei getter.
L'adattatore ricontrolla l'immagine completa ancora inattiva e le lease.
Da quel momento non richiama xb_sealed: Gate e stack bassi sono mutabili.
I contratti di codice verificato e lifetime congelato restano vincolanti.

Si riusa la sequenza INIT/SIPI host-tested. Il ready è il DWORD Gate a
offset 104 letto attraverso il prestito esistente, WB/RO/NX, della sola
pagina dati posseduta. Una funzione Assembly esegue un load DWORD allineato;
non si copia l'intera pagina mentre l'AP la modifica. Gli store del blob
e il load del BSP seguono il modello hardware x86 WB: Intel 253668-078US
§§9.1.1 e 9.2.2. Il confine di chiamata Assembly impedisce ottimizzazioni
C della lettura condivisa; nessun RMW su una pagina in sola lettura.

Solo Gate=1 autorizza l'unico NMI di ingresso, via xAPIC fisico/unicast.
ICR +300h/+310h, delivery status bit12, ESR +280h: SDM §§11.6.1 e 11.5.3.
INIT 4500h, SIPI 4600h|vettore secondo §9.4.4.1; NMI 4400h con mode100,
level assert14 e trigger edge15=0. I registri sono UC già posseduti dal
backend panic. Nessuna IPI broadcast o risorsa MMIO nuova.

Dopo il NMI si osservano solo coordinatore Rust e stato atomico per-CPU.
PARKED deve corrispondere a provider accettato e ammissione panic. Errori,
clock fermo, budget di polling esaurito o timeout causano quarantena e
arresto diagnostico con risorse trattenute. Il prototipo usa un limite di
un secondo, inferiore al mezzo periodo del PM a 24 bit, e polling finito;
è un limite diagnostico QEMU, non una promessa di latenza hardware.

Il negativo senza NMI lascia l'AP nel loop reale con IVT pronta e richiede
timeout/quarantena, senza dichiararlo online. Accettazione positiva:
dieci boot UEFI con AP osservato nei registri e nella memoria propri,
root/GS/GDT/TSS/IDT, PRIVATE/PARKED e maschera panic coerenti. Quattro vCPU
configurate non diventano quattro CPU del sistema: il traguardo è BSP+1.
Restano separati gli altri AP, il panic remoto e hardware fisico.
