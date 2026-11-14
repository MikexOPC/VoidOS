# Allocatore fisico — design

I documenti completi precedenti sono in `kernel/mm/physical/documentation/` al commit `49f8cef`.

Il pool importa e convalida i descrittori di RAM ammessa.
Mantiene extent liberi, lease vive e pin. Una lease porta base,
numero di pagine, ID e identità dell'owner. I metadati hanno
un ticket lock per istanza. L'allocazione verifica allineamento
e limite prima di aggiornare la mappa libera. Il bootstrap può
riservare una pagina bassa per l'ingresso AP. Le query di budget,
intervallo, pin e generazione leggono snapshot brevi sotto lock.

Map, unmap e shootdown appartengono al backend. Il percorso
di release dopo unmap aspetta la revoca delle traduzioni.
Se non è possibile reinserire il range libero, il pin resta
conservato. La generazione è un tag di invalidazione di cache,
mentre ID e pin stabiliscono proprietà e lifetime. Nessun
callback hardware deve girare sotto il lock dei metadati.

Il pool è usato in ordinary. Le fixture con guasti e contesa
host sono prove separate. L'header limita il pool a 128 regioni,
256 extent e 64 lease simultanee.

[Validazione del modulo](k2-pool-validation.md).
