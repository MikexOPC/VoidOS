# Processi x86-64

Il backend esegue ELF statici in ring 3 sul core di avvio, gestisce
l'ingresso delle syscall e la copia fra kernel e utente. Restituisce
al kernel l'esito di uscita, fault, frame rifiutato, immagine respinta
o scadenza del budget. Il kernel recupera le risorse dopo il ritorno
al proprio spazio di indirizzamento.

**Interfaccia.** Il kernel apre il contesto con `x86_process_open`,
esegue un'immagine con `x86_process_run` e rilascia il contesto con
`x86_process_close`. L'interfaccia residente permette alla shell di
inviare una richiesta e riprendere un singolo servizio fidato.
[ABI e contratti](../../../../docs/design/processi.md).

**Immagine ordinaria.** Con SMEP e SMAP il boot esegue e raccoglie un
ELF incorporato con `write` ed `exit`. **Profili separati.** Il
diagnostico K6 prova due contesti alternati e IPC. La shell PS/2 di `ordinary` usa
un servizio residente per `bridge status`.
[Validazione](../../../../docs/validation/processi.md).

**Limiti.** Un solo contesto attivo sul BSP, syscall a interrupt
disabilitati, immagine kernel mappata nello spazio del processo.
Caricamento da file, concorrenza generale e KPTI restano aperti.
