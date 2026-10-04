# Checkpoint: durata del timer e consumo degli eventi

Validazione del 4 ottobre 2026. Il blocco separa inizializzazione,
riarmo, consumo e chiusura della source LAPIC, prima dello scheduler.

## Implementazione e revisione

Phase e Consumed rendono esplicita la durata dell'evento. Rust verifica
le combinazioni di Count, Consumed, Phase e Active, incluso il confine
u32 senza wrap. Il gestore pubblica Pending dopo stop ed EOI; solo
il consumer fuori dall'IRQ torna allo stato Ready. Non si puo riarmare
un evento pendente, consumarlo due volte o riaprire una source chiusa.

Il riarmo richiede un token effettivamente vivo nel registro IRQ,
senza fault e con Busy=0. La revisione ha aggiunto questo controllo
oltre alla verifica locale ID/vettore: un token conservato dopo unbind
non autorizza a riattivare la source. La query non modifica il registro.

Il consumo richiede output u32 mappato, scrivibile e disgiunto; verifica
bounds, allineamento e alias prima di pubblicare. L'owner cresce di
8 byte, da 136 a 144. Non vengono allocate nuove pagine o code dinamiche.
Chiusura normale e ripristino hardware richiedono unbind quiescente
e tutti gli eventi consumati. Gli errori restano terminali: non si
dichiara un recupero automatico o la riapertura di owner in fault.

## Evidenza finale

- Test host C e 58 test Rust superati, somma verificata dai log.
- Mille cicli arm/service/take sullo stesso owner, con fixture del
  token; doppio riarmo, doppio consumo e chiusura pending respinti.
- Output alias/disallineati, scritture fallite e token non vivo respinti.
- Ultima consegna u32 consumata; nessun riarmo dopo saturazione.
- Registro reale testato con token vivi, scaduti, riutilizzati, handler
  in fault e callback gia in esecuzione.
- 23 scenari QEMU ricompilati sullo stato finale: 128/256 MiB, smp4
  con BSP attivo, IRQ, NX, guardie, NMI e double fault ancora verdi.
- Count=Consumed=3, Phase=4, token vuoto; tre riarmi pendenti e tre
  secondi consumi rifiutati durante la prova nativa.
- Restano verificati tre risvegli HLT, RIP dopo HLT e IF=1 nella traccia,
  controller fermo e ripristino LVT/divisore al termine riuscito.
- Verificatore: 14 alterazioni timer, 37 TX e 16 registro IRQ respinte.
- Immagine valida e 7 mutazioni invalide verificate; warning come errori.

Log: `kernel/build/events-host-final.log`, `events-firmware-final.log`,
`events-image.log`, `firmware-98a85e1b0a9242d0b2e2f12a23fab298/`.
ELF verificato e installato nella ESP: 126.976 byte, SHA256
`2750FA672DF1F05024A7969C63BD9DEBDD1A519333CF42502D38B88BD777E93E`.

## Limiti e prossimo passo

La prova di bootstrap rimane finita: chiude il timer prima di
KERNEL_READY. Le nuove API rendono riusabile la durata della source,
ma non introducono ancora servizio permanente, scheduler, user mode,
SMP attivo o validazione su hardware fisico. HLT non offre un timeout
senza una sorgente di risveglio indipendente; QEMU mantiene il timeout
esterno. Il prossimo blocco deve integrare la durata permanente nel
ciclo kernel e definire arresto e politica di timeout/watchdog.

Contratto e manuale Intel: [eventi del timer](../../kernel/timer/events.md).
