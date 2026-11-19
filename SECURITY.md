# Policy di sicurezza

VoidOS è un sistema operativo sperimentale in sviluppo. Il riferimento
per le verifiche è il branch `main`; ogni segnalazione deve identificare
il commit e il profilo coinvolti. Anche una riproduzione su una revisione
precedente è utile, specificando l'eventuale esito su `main`.
Capacità provate, protezioni e limiti correnti sono nello
[stato del progetto](docs/status.md) e nella
[validazione della sicurezza](docs/validation/sicurezza.md).

## Segnalare una vulnerabilità

Usa **Report a vulnerability** nella pagina
[Security advisories di VoidOS](https://github.com/MikexOPC/VoidOS/security/advisories)
quando il pulsante è disponibile. La disponibilità dipende dalle
impostazioni di segnalazione privata del repository.

Se il pulsante è assente, apri una
[issue di richiesta contatto](https://github.com/MikexOPC/VoidOS/issues/new)
con il solo testo «Vorrei concordare un canale riservato per una
segnalazione di sicurezza». Il referente è
[MikexOPC](https://github.com/MikexOPC). Attendi di concordare il canale
privato prima di condividere dettagli tecnici, prove di sfruttamento o
dati sensibili. Questo percorso segue le
[indicazioni di GitHub per le segnalazioni riservate](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/report-privately).

Nel canale riservato includi commit, profilo, ambiente e prerequisiti,
passi di riproduzione, impatto osservato, risultato atteso e log pertinenti.
Una riproduzione minima aiuta a verificare il difetto. Rimuovi credenziali,
dati personali e informazioni estranee prima dell'invio. Concorda con il
maintainer tempi e contenuto della divulgazione pubblica.

## Confini e problemi da segnalare

La policy copre loader UEFI, kernel, backend x86, driver, librerie,
programmi utente e strumenti di build e test presenti nel repository.
Firmware, dispositivi, file caricati e dati da ring 3 possono fornire input
non affidabili. I confini rilevanti comprendono il passaggio al kernel,
gli accessi alla memoria e il possesso e rilascio delle risorse.

Segnala corruzione o divulgazione di memoria, violazioni dei permessi,
superamento dei confini di privilegio, uso di risorse dopo il rilascio,
input che provocano comportamenti non sicuri e compromissioni della
build. Descrivi accesso necessario e percorso raggiungibile. Specifica
se la prova riguarda l'immagine `ordinary`, un profilo diagnostico,
un test host o hardware fisico.

I limiti già documentati aiutano a contestualizzare l'impatto.
Segnala comunque comportamenti nuovi o conseguenze ulteriori osservate.
