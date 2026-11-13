# Sessione video VirtIO con Intel VT-d

Il modulo serve i profili interattivi con `KERNEL_VGPU_IOMMU`,
tra cui `v2c`. Il [contratto fra moduli](../../../../../../../../docs/design/consolidamento.md#video-vt-d-e-ps2)
e la [validazione](../../../../../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86)
descrivono il perimetro. I documenti precedenti completi sono in
questa cartella al commit `8bad38a`.

## Owner e stati della sessione

`X86_VGPU_SESSION` possiede arena DMA, mapping della superficie,
budget, accounting e oggetto `X86_VGPU_PRESENT`. Il mapping PCI e
la directory ACPI arrivano dal chiamante. L'oggetto present
contiene trasporto, coda, scanout, target e autorità della superficie.
La console riceve un `KERNEL_DISPLAY` per controllare disponibilità,
polling, rettangoli e redraw.

| Stato | Significato |
| --- | --- |
| OFF | Nessuna sessione attiva. Assenza GPU o rifiuto del routing possono lasciare disponibile il GOP. |
| TRANSLATING | Dominio attivo e risorse contabilizzate. La disponibilità del target video dipende anche dallo stato di present. |
| COMPROMISED | Il polling rileva fault VT-d o non può confermare uno stato pulito. La GPU viene ritirata. |
| RETAINED | Stop, traduzione, rilascio o accounting non confermati. Il chiamante riceve errore. |

## Avvio

Il BSP risolve la GPU nel dominio VT-d usando il routing ACPI/PCI,
legge il budget e acquisisce arena e superficie. Abilita la
traduzione prima del bring-up della GPU. Se un passo fallisce,
tenta di disabilitare la traduzione e di ritirare le risorse
acquisite. Le risorse incerte restano trattenute. Il target della
console diventa disponibile dopo negoziazione, configurazione
della coda, creazione e associazione della risorsa e scanout.
Il profilo di misura senza presentazione lascia la console sul GOP.
Le lease ancora vive entrano nel registro dei detentori.

## Presentazione e fallback

Il worker disegna nella superficie autorizzata e passa rettangoli
limitati alla geometria attiva. C'è un solo aggiornamento in volo.
Il danno accumulato nel frattempo alimenta il prossimo aggiornamento.
Lo scanout invia TRANSFER_TO_HOST_2D e poi RESOURCE_FLUSH per lo
stesso rettangolo. Il polling verifica prima il completamento,
poi la scadenza. La console continua a chiamare poll finché
l'oggetto segnala lavoro. Dopo 100 ms il ritardo è registrato una
volta. A un secondo il timeout ritira il target e consente il GOP.

Il recupero dopo timeout riusa il trasporto e la coda nativa
salvati, ripete negoziazione e scanout, ricontrolla l'autorità della
superficie e ridisegna. Il clipping segue il target attivo. Un
fault VT-d o un errore definitivo del protocollo ritira la GPU.
Questa politica non dichiara recuperabile un dispositivo con reset
o stato DMA non confermato.

## Arresto e fonti

La sessione chiude present e verifica lo stop/reset della GPU
prima di disattivare la traduzione e invalidare il dominio.
Quindi ritira superficie e arena, con la barriera di paging
prevista per i mapping CPU, e verifica il budget. Il registro
dei detentori viene aggiornato dopo il rilascio. Un fallimento
con risorse vive mantiene RETAINED.

Riferimenti VirtIO 1.2 §§2.7, 3.1 e 5.7 per coda, negoziazione,
reset e GPU 2D. Intel VT-d 5.0 §§7–8 e 11 per traduzione, DMAR
e interfaccia registri. I backend comuni e VT-d dichiarano le
capability e i bit accanto agli accessi. Il profilo provato usa
QEMU/OVMF e la GPU emulata. GPU fisiche, più unità e RMRR sono
fuori dal perimetro verificato.
