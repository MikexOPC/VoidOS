# K3-I — prova NMI remota sul primo AP

Collaudo del protocollo panic SMP con NMI diretti già scelto dall'utente.
Resta il diagnostico con BSP e un solo AP: nessun nuovo protocollo di
avvio, riuso della transizione o cambiamento del boot ordinario.

Dopo PARKED/provider accettato/maschera online 3, il BSP invoca il panic
volontario esistente con una ragione numerica. Il backend elegge il BSP,
indirizza il solo AP online e attende il suo ACK entro i limiti esistenti.
L'AP riceve il secondo vero NMI, questa volta tramite IDT/IST2 definitive,
pubblica il record remoto e si arresta. Nessuna nuova operazione MMIO.

Il profilo richiede esplicitamente launch positivo e rifiuta NoNmi.
Il runner distingue il panic intenzionale da qualsiasi altro FAIL,
attende marker CPU e scarico seriale, poi verifica registri di entrambi
i core, maschere/record, IST2 privato, proprietà e budget delle risorse.
PARKED precede le ultime istruzioni del parcheggio: il RSP interrotto
può ancora trovarsi dentro la chiamata C, quindi si verifica l'intero
stack privato autorizzato, non un singolo RSP arbitrariamente imposto.

Accettazione: dieci boot consecutivi, owner BSP, targets=ACK=2,
missing=outcome=0, due record completi, IF=0 nel contesto interrotto,
nessun record dei due AP mai avviati. Poi regressione launch senza panic.
Il test non dimostra ancora origine panic da AP o più AP online.

Fonti già applicate al backend: Intel SDM 253668-078US §§6.7.1,
6.14 (NMI/IRET/IST) e 11.6.1 (ICR NMI fisico unicast 4400h).
https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf
