# Risorse VirtIO GPU — design

I documenti completi precedenti sono in `kernel/drivers/video/virtio/resources/documentation/` al commit `49f8cef`.

Il core comune prepara una risorsa 2D, associa il backing e
collega lo scanout tramite il trasporto e la coda già validati.
Il backend garantisce vita e proprietà del backing. L'operazione
di update usa rettangoli all'interno della superficie con stride
completo: TRANSFER_TO_HOST_2D precede RESOURCE_FLUSH.
Post e settle distinguono richiesta in volo e commit confermato.
Unbind disattiva lo scanout prima di staccare il backing.
Su risposta incerta la memoria resta trattenuta fino a stop
o reset confermato. VirtIO 1.2 §5.7.6 descrive il protocollo.

Le prove di risorse e scanout sono profili separati dalla build
ordinaria. La [sessione x86 con VT-d](../../../../../../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)
stabilisce proprietà dell'arena DMA, fallback e recovery della
console. Il core comune non introduce un driver per GPU fisiche.

[Validazione del modulo](validation.md).
