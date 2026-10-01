# Capacita e limiti numerici dei buffer

`lib/memory/buffer_capacity.rs` e un modulo Rust `no_std`, collegato
staticamente in `os_memory.lib`. L'interfaccia C e in `buffer_capacity.h`.
Non riceve puntatori, non alloca, non usa uno stato globale e non accede
alla memoria dei chiamanti.

## Capacita con margine

`os_buffer_capacity(needed, entry_bytes, extra_entries, alignment, maximum)`
restituisce la capacita con margine e allineamento, oppure zero su errore.
`needed` ed `entry_bytes` devono essere positivi; l'allineamento deve
essere una potenza di due. Il margine puo contenere zero elementi.

Moltiplicazione, somma e arrotondamento usano aritmetica controllata.
Un overflow o una capacita oltre il massimo produce zero senza panic.
Non viene troncato un risultato fuori limite per farlo sembrare valido.

Il buffer temporaneo UEFI usa otto descrittori aggiuntivi e allineamento 1.
Il buffer persistente usa lo stesso margine e allineamento 4096.
I chiamanti mantengono la verifica del profilo UEFI: stride 40..4096,
massimo 1 MiB e traduzione del risultato negli status EFI appropriati.

## Estremo del buffer

`os_buffer_extent_is_valid(base, capacity, maximum)` restituisce 1 se
base e capacita sono positivi, il budget e rispettato e l'estremo
esclusivo e rappresentabile in 64 bit. Altrimenti restituisce zero.

E un controllo numerico: non dimostra che un indirizzo sia mappato,
scrivibile o ancora vivo. In C il chiamante deve garantire questi
requisiti e la durata dei dati; i controlli sugli alias restano separati.

## Verifiche

Cinque test Rust confrontano i risultati con un oracolo a 128 bit:
11520 combinazioni di confini, 10000 casi generati e i profili del boot.
Coprono overflow del margine, della somma e dell'allineamento, budget,
allineamenti invalidi e intervalli numerici al limite dei 64 bit.
Il test C++ del boot verifica anche chiamata e ritorno attraverso la ABI C.

[Rust: aritmetica controllata degli interi](https://doc.rust-lang.org/core/primitive.u64.html#method.checked_add).
