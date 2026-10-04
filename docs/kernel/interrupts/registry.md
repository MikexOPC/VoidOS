# Registro persistente degli handler IRQ

Il dispatcher ring zero non dipende piu dal driver COM1. Un registro
kernel statico di 120 byte gestisce due subscription: vettore 0x40 per
COM1 e 0x41 per il timer LAPIC calibrato, provato con tre scadenze finite.
INT 0xf0 e il vettore spurious 0xff restano percorsi separati senza EOI.
Un vettore senza subscription valida arresta il kernel.

## Registrazione e durata

Initialize richiede memoria vergine, IF=0 e intervallo codice fidato.
Self rende il registro non trasferibile tramite copia. Bind pubblica un
handler soltanto con token vergine disgiunto dal registro, slot libero,
callback non nulle nell'intervallo dichiarato e dispositivo quiescente.
Il registro non sostituisce implicitamente una subscription viva.
I token di 16 byte contengono ID monotono, vettore e Reserved=0.
Next non va in wrap: l'esaurimento rifiuta nuove registrazioni.
Token-live verifica ID e vettore correnti, assenza di fault e Busy=0
prima di consentire al driver timer un nuovo riarmo. Non basta che
un token conservato contenga un ID nonzero.

Le callback sono funzioni kernel fidate con durata pari al kernel;
gli indirizzi sono confrontati con l'intervallo immagine, senza pretendere
che questo confronto da solo dimostri permessi eseguibili o autorizzi
codice utente. Il chiamante garantisce memoria mappata e valida per owner
e token; gli intervalli escludono MMIO LAPIC, finestra e vista ricorsiva.
Non c'e caricamento dinamico di driver o allocazione in questo modulo.

Unbind richiede token corrente, IF=0 e callback Quiet positiva.
Quiet deve provare che sorgente e rotta sono chiuse e non ci sono IRQ
in servizio o pendenti. Solo allora slot e token vengono azzerati.
Un vecchio token non puo rimuovere una registrazione successiva.
Unbind rifiutato conserva token e subscription.

## Consegna e responsabilita

Il profilo Rust verifica vettori ammessi, fault bounded e ID < Next.
La consegna richiede una subscription valida, IF=0, nessuna callback
gia in corso, nessun fault precedente e contatore non esaurito.
Busy impedisce registrazioni, rimozioni e consegne rientranti anche
durante Quiet. Il contratto rimane UP: Busy non e un lock per SMP.
Le callback non devono abilitare IF o modificare direttamente il registro.

Handle restituisce uno solo dopo aver gestito il dispositivo ed EOI.
Il registro incrementa Delivered dopo il successo; su errore imposta
Fault persistente e impedisce nuove consegne. Non invia EOI generici
e non tenta di recuperare autonomamente una sorgente sconosciuta.
Il dispatcher arresta il kernel se una consegna fallisce.

COM1 registra il proprio handler prima di aprire la rotta. Dopo TX,
chiude IER e ripristina OUT2, maschera la rotta e verifica LAPIC idle.
Poi rimuove la subscription e confronta Delivered con il contatore UART.
Se la rimozione fallisce, il mapping LAPIC non viene liberato sotto
un handler ancora registrato. Il registro rimane disponibile nel kernel.

## Evidenza e limiti

Host verifica due subscription indipendenti, ID e token scaduti,
1.000 cicli, rientro vietato in Handle e Quiet, callback fuori intervallo,
IF, alias, corruzione, mancata quiescenza e saturazione dei contatori.
Due nuovi test Rust verificano vettori, fault e generazioni.
QEMU confronta tutte le 140 consegne COM1 e tutti i 120 byte del registro
finale: owner vivo, Next=3, Busy=0 e slot azzerati dopo rimozione dei due driver.

Questo e il primo blocco del piano IRQ persistenti: il registro ha durata
kernel, ma l'esecuzione resta la prova finita di bootstrap. Non sono ancora
presenti un event loop con IF=1 stabile, timer permanente, scheduler, ritorno
user mode o supporto SMP. Hardware fisico non testato.

[Intel SDM: interrupt gate, IF e ritorno](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
Il gate hardware azzera IF all'ingresso; il registro non sostituisce
le verifiche del frame, dello stack e della IDT.
