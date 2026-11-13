# Paging x86-64

Gestisce accesso e inventario delle tabelle kernel, permessi delle
mappature e revoca delle regioni transitorie. I mapper collegano una
risorsa posseduta alla sua authority. Il riuso dopo la revoca richiede
che gli AP attivi confermino il flush TLB.

**Interfaccia.** L'inventario descrive le foglie attese e verifica
che i mapper conservino le lease e i permessi. La revoca dello slot
basso avviene quando stack e descrittori kernel sono attivi.
[Design](../../../../docs/design/consolidamento.md#layout-e-risorse).

**Immagine ordinaria.** Paging con NX e WP, revoca della capsula e
inventario kernel nel percorso QEMU/OVMF. **Profili separati.** I
negativi dedicati verificano accessi a mapping revocati e rilascio
con ACK assente. [Validazione](../../../../docs/validation/consolidamento.md#boot-memoria-e-dispositivi).

**Limiti.** Le pagine del loader restano riservate. W^X su tutte le
sezioni dell'immagine e comportamento su hardware fisico non verificati.
