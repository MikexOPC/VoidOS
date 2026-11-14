# Idle e dispatcher — design

I documenti completi precedenti sono in `kernel/scheduler/idle/documentation/` al commit `49f8cef`.

Il dispatcher rimane proprietario dello stack di controllo quando
nessuna task è pronta. Può fare idle in una finestra x86 che abilita
gli interrupt e verifica stop, eventi pendenti e vincoli della
sessione al ritorno. Un timeout HPET può restituire il controllo
anche se la sorgente attesa non progredisce.

I worker persistenti osservano l'epoca prima di attendere. Un
evento intervenuto fra lettura e richiesta di wait mantiene la
task eseguibile. Gli eventi arrivano al dispatcher prima della
selezione. Lo stop sveglia quanto serve per chiudere la sessione.
Token e stack restano posseduti fino a cleanup verificato.
L'idle ordinario a boot finito ha un perimetro circoscritto.
Le sessioni continue, rollover e stop esterno sono prove dedicate.

[Validazione del modulo](validation.md).
