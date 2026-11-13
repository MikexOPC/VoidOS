# GUI di VoidOS

`ui/` ospiterà il compositor e il desktop. `apps/` ospiterà le
applicazioni, a partire dal terminale quando può girare come processo
separato. Il primo collaudo mostrerà in QEMU un riquadro disegnato da un
programma Rust in ring 3.

## Proposta per il primo riquadro

Il kernel avvia un solo programma grafico diagnostico e gli concede
un riquadro da 32 × 32 pixel. Il programma prepara 4 KiB di pixel in
memoria utente e chiede di inviarli al display. Il kernel possiede
posizione, formato, superficie VirtIO e trasferimento al dispositivo.
Il permesso appartiene al processo avviato dal kernel.

```text
Programma ring 3 → copia verificata → buffer kernel → superficie VirtIO → schermo
```

Il kernel copia i 4 KiB con il percorso protetto già usato dalle
syscall, in blocchi di massimo 256 byte. Pubblica il riquadro solo
dopo la copia completa. Una richiesta invalida o un fault di copia
lasciano intatto il riquadro già visibile. Il proprietario del display
serializza gli aggiornamenti. Se il trasferimento VirtIO fallisce,
il kernel chiude la prova e restituisce il controllo del display alla
console secondo il percorso di recupero.

La console VirtIO già trasferisce e presenta rettangoli modificati
da una superficie kernel. Il diagnostico può riusare quel percorso.
[Design della presentazione VirtIO](../../arch/x86/kernel/drivers/video/virtio/iommu/documentation/design.md)

## Memoria condivisa

Una superficie mappata in userspace permette di disegnare frame più
grandi evitando la copia alla consegna. Richiede regole per la durata
del mapping e per il riuso del buffer dopo la presentazione. I buffer
dumb di DRM e il rilascio di `wl_buffer` in Wayland offrono riferimenti
per quella fase. Propongo la copia per il primo collaudo e la memoria
condivisa quando serve ridisegnare finestre intere.
[Linux DRM/KMS](https://docs.kernel.org/gpu/drm-kms.html#dumb-buffer-objects)
[Protocollo Wayland](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_buffer)

## Criteri di accettazione

- Due richieste dallo stesso processo producono due immagini diverse.
  Il runner confronta pixel attesi dentro il riquadro e pixel invariati
  subito fuori.
- Il kernel respinge un chiamante senza permesso, una lunghezza errata
  e un puntatore che attraversa una pagina assente. Dopo ogni rifiuto
  l'immagine precedente resta visibile.
- All'uscita il kernel recupera le pagine e la console torna operativa.
  Il collaudo richiede dieci boot consecutivi del profilo VirtIO.
