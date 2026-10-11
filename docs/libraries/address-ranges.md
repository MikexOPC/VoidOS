# Confronto degli intervalli

`os_address_ranges_overlap(first, first_bytes, second, second_bytes)`
restituisce 1 se due intervalli numerici si sovrappongono, 0 altrimenti.
La lunghezza zero rappresenta un intervallo vuoto; due intervalli
adiacenti non si sovrappongono.

La funzione confronta la distanza tra le basi con la lunghezza del primo
intervallo incontrato. Non somma base e lunghezza, non dereferenzia
indirizzi e non cambia la proprieta della memoria.

Gli intervalli sono lineari: un'estensione oltre `UINT64_MAX` non torna
all'indirizzo zero. Il confronto puo quindi essere usato anche prima di
validare un indirizzo restituito dal firmware. Il risultato non prova
che un intervallo sia rappresentabile, mappato o accessibile: il chiamante
verifica separatamente questi requisiti prima degli accessi alla memoria.

Nel loader la funzione sostituisce confronti locali in tre componenti:

- caricamento ELF: alias con file, proprietari e allocazioni gia acquisite;
- mappa di memoria: descrittori fisici sovrapposti;
- piano di paging: alias fisici e sovrapposizioni virtuali.

`test/lib/memory/address_ranges_test.c` verifica intervalli vuoti,
adiacenza, contenimento, simmetria e indirizzi al limite dei 64 bit.
Verifica inoltre 73984 combinazioni di piccoli intervalli contro un
oracolo che enumera i singoli indirizzi, separato dall'algoritmo usato
in produzione. La build esegue questi test insieme alle regressioni
ELF, memoria e paging.
