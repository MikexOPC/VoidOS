# V1: GPU VirtIO sotto traduzione VT-d (`VIRTIO_F_ACCESS_PLATFORM`)

23 ottobre 2026. Decisioni approvate: W1-A indirizzi device separati dai frame,
W2-A traduzione temporanea nel profilo diagnostico, W3-A senza IOMMU il bit 33
non viene mai accettato. Per il conflitto sulla page window: P1, parcheggio e ripresa.

## Cosa dimostra

Nel solo profilo diagnostico `-TestVgpuIommu` la GPU (00:01.0, SID 0x08) lavora con il
VT-d acceso e un dominio privato che contiene solo le sue pagine:

- **Coda:** 4 pagine in lettura/scrittura, IOVA 0x1001000.
- **Backing della superficie:** 1000 pagine (1280x800) in sola lettura, IOVA 0x1006000.

I ring, i descrittori e l'ATTACH_BACKING pubblicano IOVA. Il diagnostico visivo
esistente (due checkpoint con screenshot completi) passa identico, la GPU non produce
fault e il trace QEMU mostra 00:01.0 sotto traduzione. Nel profilo
`-TestVgpuIommuFault` il backing viene revocato dopo un ciclo completo: il secondo
ATTACH è rifiutato e la GPU registra un fault di lettura (FR 6) sulla prima pagina del
backing, riconosciuto con `x86_vtd_activation_expect`.

## Modifiche

- `kernel/drivers/video/virtio`: `VGPU_BUFFERS.Device[4]` e `VGPU_RESOURCE.Device`.
  Il driver pubblica solo questi indirizzi; i frame `Physical`/`Base` restano per la CPU
  e per i controlli di sovrapposizione. Senza IOMMU sono identici ai frame.
- `initialization`: `KERNEL_VGPU_IO.Platform` e policy Rust `features(offered, platform)`.
  Con `platform=1` ACCESS_PLATFORM è accettato **e richiesto** (un device senza il bit
  aggirerebbe l'IOMMU); con `platform=0` non è mai accettato, come prima.
- `arch/x86/mm/window`: `x86_page_window_park` e `x86_page_window_resume`. Il parcheggio
  toglie le PTE e consegna il lease pinned al chiamante; la ripresa rimappa lo stesso
  lease allo stesso indirizzo, senza azzerarlo. Lo snapshot dell'inventario paging
  include lo stato della window, quindi dopo un parcheggio va ricatturato prima di usare
  il VT-d.
- `domains`: `x86_vtd_domain_translate`, una pura ricerca di IOVA su buffer vivi con
  l'accesso richiesto.
- `test/.../virtio/iommu/`: collegamento diagnostico (ECAM, topologia, DMAR, route della
  GPU, mapping registri, QI, dominio, attivazione); il build linka i moduli verificati
  senza le loro probe.

Sequenza: superficie → route (sole letture) → negoziazione con il bit 33 → acquisizione
della window → parcheggio → dominio + QI + TE=1 → ripresa → code e scanout →
reset del device confermato → parcheggio e ricattura → TE=0, flush, rilascio → lease
della coda liberato.

## Evidenze

| Prova | Risultato |
|---|---|
| Page window host | casi di parcheggio/ripresa nella suite `test/kernel/host.ps1` |
| VirtIO init host | 98.100+ controlli, compresi i casi ACCESS_PLATFORM e il test Rust `access_platform` |
| VirtIO code/risorse host | 646.546 coda, 10.000 risorse, 19.900 presentazione: i mock verificano che ring, descrittori e ATTACH usino `Device` |
| Dominio host | 976.241 controlli, compresa la traduzione |
| QEMU V1 visivo, 10 boot consecutivi | 10/10 PASS: `kernel/build/virtio-initialization-firmware-d10c9b7cafae45be8528a7cfd8841b6b` |
| Verificatore `scripts/iommu.py` | 8 mutazioni respinte (profilo scambiato nei due sensi, bit 33 assente, RETAINED, GPU non tradotta, record QEMU mancante, pagina errata, flush di revoca mancante) |

## Limiti

- Solo QEMU TCG; una unità VT-d; nessun hardware fisico.
- Nello stesso build la probe delle code (richiesta dal profilo risorse) gira **prima**
  del dominio, con ACCESS_PLATFORM negoziato e TE spento: quelle 256 completamenti non
  sono tradotti. La prova di traduzione riguarda la sessione risorse/scanout.
- QEMU mappa il backing all'ATTACH: una revoca dopo l'ATTACH non bloccherebbe i
  TRANSFER successivi in QEMU. La prova negativa revoca quindi prima dell'ATTACH.
- Invalidazione globale, fault in polling, traduzione temporanea (W2-A).
- Il limite del dominio (4100 pagine) basta per 16 MiB di backing più la coda, senza margine.
- Coverage N/D; ASan solo host.

Riproduzione: `kernel/scripts/build.ps1 -TestVgpuInitialization -TestVgpuScanout -TestVgpuIommu`;
`python -B arch/x86/kernel/drivers/video/virtio/scripts/initialization.py --scanout --iommu --image <kernel.elf> --boots 10`;
negativo: `-TestVgpuInitialization -TestVgpuIommuFault` e `--iommu-revoke`.
