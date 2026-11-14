# Framebuffer comune — design

I documenti completi precedenti sono in `kernel/drivers/video/framebuffer/documentation/` al commit `49f8cef`.

Il loader seleziona GOP e consegna una descrizione del buffer
nel contratto di avvio. I validatori comuni controllano dimensioni,
stride, capacità, formato RGB/BGR e posizione fisica, con
aritmetica checked. Per il mapping calcolano offset interno,
pagine ed estremi senza pubblicare PTE. Il backend x86 è owner di
lease, tabelle, guardie e mapping UC e verifica i permessi prima
di rendere disponibile il target.

L'ordinaria adotta il descrittore se presente. La shell ordinaria può usare GOP. I probe di disegno usano profili dedicati. Il preflight
numerico non attribuisce ownership o accessibilità a un indirizzo.
L'assenza è un esito ammesso. Un rollback parziale conserva le
risorse non ancora revocate.

[Validazione del modulo](validation.md).
