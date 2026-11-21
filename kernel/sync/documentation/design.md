# Sincronizzazione — design

I documenti completi precedenti sono in `kernel/sync/documentation/` al commit `49f8cef`.

Il ticket lock mantiene Next e Serving e usa le atomiche Rust
per pubblicazione e acquisizione. Il contesto corrente appartiene
alla CPU. L'acquisizione con irqsave registra IF e un guard, verifica
rango e profondità e attende il ticket entro il budget del clock.
Il budget decorre dopo la prenotazione e si rinnova quando `Serving`
avanza. Misura il mancato progresso del proprietario corrente. Un'attesa
in coda può attraversare più proprietari prima di ricevere il proprio
ticket. La scadenza e il limite di poll restano applicati a ogni intervallo
senza avanzamento. Un ultimo acquire di `Serving` copre il passaggio di
proprietà contemporaneo alla scadenza.
La release è LIFO e ripristina IF dal guard. Il contesto ha
profondità massima otto. Lock con owner e rango validi delimitano
i metadati di memoria e altri oggetti condivisi. Hardware o
callback lunghi restano fuori dalle sezioni critiche di memoria.

Il bootstrap promuove il contesto precoce a quello del BSP prima
delle operazioni che richiedono lock runtime. Gli AP portano il
proprio contesto GS. Nel boot ordinary il lock è usato dai moduli
integrati; i probe di annidamento, timeout e acquisizione errata
sono diagnostici. IF disabilitato protegge il contesto locale,
mentre ticket e atomiche sincronizzano fra CPU.

[Validazione del modulo](validation.md).
