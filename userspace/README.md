# Userspace

- [`init/`](init/) contiene il primo programma Rust. Il kernel lo
  incorpora come ELF, lo esegue in ring 3 durante il boot e ne recupera
  le risorse all'uscita. Lo script di link è condiviso con i payload
  diagnostici K6.
- [`lib/`](lib/) raccoglie il contratto previsto per le librerie utente.
- [`services/`](services/) raccoglie il contratto previsto per i servizi
  utente. Il [bridge](../bridge/) ha una cartella propria alla radice.
