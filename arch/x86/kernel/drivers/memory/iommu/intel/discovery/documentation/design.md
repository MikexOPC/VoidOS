# Intel VT-d: discovery DMAR

20 ottobre2026. Decisione utente: prima IOMMU, poi console VirtIO ordinaria;
primo backend Intel VT-d su QEMU. Nessuna estensione dell'eccezione IOMMU
alla console interattiva. Questo primo modulo legge soltanto la descrizione
firmware; non abilita traduzione, interrupt remapping o DMA GPU.

## Fonti e ricerca

[Intel VT-d rev5.0, D51397-017](https://cdrdv2-public.intel.com/831418/vt-directed-io-spec.pdf),
capitolo8, sezioni8.1/8.2/8.3/8.3.1, pagine8-1..8-5. Copia verificata in
build/iommu-research; SHA256
`A136448F2A14D49AA524106F58A139A5ADD90F78EEF8A6ABAABCC917EBC753B6`.

DMAR: header48 byte, revision1, checksum intero; HAW byte36 codificato N+1,
flags37 bit0 INTR_REMAP, bit1 X2APIC_OPT_OUT, bit2 DMA_CTRL_PLATFORM_OPT_IN;
reserved38..47 a zero. Record: type/u16 a0, length/u16 a2, ordine numerico.
DRHD: minimo16; flags4 bit0 INCLUDE_PCI_ALL; Size5 bit3:0, span4096<<Size;
segment/u16 a6 e register base/u64 a8 allineata allo span. La revisione5.0
descrive Size: non trattare sempre il byte5 come reserved o assumere4KiB.
Device Scope: type0, length1, flags2 e reserved3 zero nel DRHD,
enumeration4, start bus5, coppie device/function da6. Type1/2 non ammessi
nel DRHD INCLUDE_PCI_ALL; path multihop non e un BDF gia risolto.

[QEMU v11.1.0 intel_iommu.c](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/intel_iommu.c)
e [ACPI build](https://raw.githubusercontent.com/qemu/qemu/v11.1.0/hw/i386/acpi-build.c)
sono il riferimento per il device emulato e la tabella esposta.
[Linux v6.12 dmar.c](https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/iommu/intel/dmar.c)
mostra la separazione tra parsing del firmware, routing PCI e attivazione;
nessun codice copiato. AMD-Vi/IVRS richiede un backend distinto.

## Contratto v0 e perimetro

Cartella funzionale: arch/x86/kernel/drivers/memory/iommu/intel/discovery/.
Adattatori limitati a ingresso diagnostico kernel, build, lettore ACPI
esistente e test. Nessuna modifica al loader o a scheduler/allocatore.

C legge i byte little endian e attraversa la tabella; Rust puro valida
HAW/flags, registro fisico/span e campi scope. Nessun unsafe, allocazione
o puntatore fisico dereferenziato. Output pubblicato solo dopo verifica
completa, disgiunto dagli input e immutato su fallimento/assenza.

Limiti:64KiB tabella,8 unita,32 scope,8 coppie PCI per scope. Tipi scope
non supportati rifiutati. Altri record DMAR saltati secondo length e contati,
senza dichiarare interpretati RMRR/ATSR/RHSA/ANDD/SATC/SIDP. La loro presenza
sara un gate per l'autorizzazione DMA finche non modellata. Nessuna scelta
di dominio viene dedotta da questo inventario incompleto.

Verificare checksum, lunghezze, riservati, ordine record, include-all unico
e ultimo nel segmento, registri non sovrapposti. Il finder legge tramite
la finestra ACPI gia esistente, verifica directory e tabella unica;64KiB
di scratch posseduto dal probe di boot, non dallo stack o da un device.
Budget e finestra idle verificati anche se il lettore fallisce.

## Pseudocodice non eseguibile

1. Verificare directory, spans e disgiunzione scratch/output.
2. Leggere header di ogni tabella; DMAR assente e un risultato esplicito.
3. Per DMAR: leggere snapshot bounded, ricontrollare header, checksum,
   campi e record. Una seconda DMAR e un errore.
4. Costruire inventario provvisorio; pubblicarlo solo alla fine.
5. Nel kernel diagnostico verificare finestra/budget invariati e loggare
   inventario. Nessuna lettura/scrittura dei registri VT-d.

Accettazione: test C/Rust/ASan su confini, campi corrotti, alias e failure
del lettore; dieci boot consecutivi con DMAR reale QEMU e confronto esterno
dei byte e unita, piu assenza IOMMU e regressione ordinaria. Build con
warning come errori, size/folder/link e review finale. Prove non eseguite
non sono risultati.

## Blocchi successivi

1. Mapping MMIO posseduto e lettura VER/CAP/ECAP, capacita e stato iniziale.
2. Dominio/routing per la GPU, root/context e tabelle second-stage pinned;
   nessun passthrough implicito di altri requester o RMRR ignorati.
3. Attivazione ordinata, invalidazioni e revoca prima del riuso della RAM.
4. DMA consentito/vietato osservato, fault e reset/cleanup reali; solo dopo
   questi gate progettare la sessione persistente della console.

Per la futura integrazione VirtIO verificare anche [VirtIO1.2, capitolo6](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html):
ACCESS_PLATFORM bit33 e il contratto degli indirizzi DMA. Il diagnostico
attuale negozia soltanto VERSION_1; non basta aggiungere intel-iommu alla
VM per dichiarare protette le richieste GPU. Serviranno configurazione
device, negoziazione, mapping IOVA e prove negative del percorso effettivo.
