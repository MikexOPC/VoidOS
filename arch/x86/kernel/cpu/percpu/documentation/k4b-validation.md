# K4-B — Validazione

## Comando IPI di flush

La mailbox Rust pubblica operazione e generazione con un solo CAS.
Il bit 63 seleziona il flush, i bit 0–62 contengono la generazione.
Il chiamante vede la generazione separata, con limite 2^63−1.
L'AP verifica di aver acquisito quella richiesta prima di leggere
l'operazione. Il layout della mailbox resta di 64 byte.

Il backend ricarica lo stesso CR3 con IRQ mascherati, root attesa e
PGE/PCIDE disattivati. L'ACK segue il reload e l'EOI. Il diagnostico
esegue 99 PING, poi un flush per AP. Il BSP ricarica CR3 nel giro finale.
Il disassemblato conferma il controllo CR4 con maschera 0x20080 e
la scrittura di CR3 prima del completamento.

- Build MSVC/NASM/Rust ordinaria e diagnostica riuscita.
- Suite ipi-service passata, inclusi 7 test Rust, ASan sui test C e cppcheck.
  Evidenza: kernel/build/test-run-a84b1a5dc5844cf69ce02cb4db6627ae.
- Layout passato. Evidenza: kernel/build/test-run-34ba894ba9a54013a1805f0e7f360c4f.
- Audit del codice di emergenza passato sull'immagine diagnostica.
- 10/10 boot diagnostici QEMU a 4 CPU. Per boot, 297 PING e 3 flush
  AP con ACK dopo EOI e verifica del contesto dopo IRETQ.
  Evidenza: kernel/build/ipi-service-e6e32cfa0e9f4048a1f2a3c33c490422.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-c36b70ed949a4be7b30abacd1da321e7.

Il comando IPI si occupa del flush. Il collaudo della rimappatura è
riportato sotto. Coverage non misurata.

## Rimappatura diagnostica

Il giro 98 legge il valore del vecchio frame su ogni AP. Il BSP
rimappa la stessa VA e il giro 99 invalida l'intero contesto. Il giro
100 legge il valore del nuovo frame su ogni AP. Il BSP rilascia il
vecchio frame dopo gli ACK e le letture.

Nei due profili di errore, AP1 esegue il flush e completa EOI al giro
99, poi omette l'ACK oppure tenta di confermare la generazione
successiva. Il runner verifica il motivo del panic e legge l'inventario
dell'allocatore a VM ferma. Entrambi i frame risultano ancora pinned
nei casi di errore. Nel caso positivo resta il frame sostitutivo,
mantenuto fino all'arresto del diagnostico.

| Profilo QEMU a 4 CPU | Esito | Evidenza in kernel/build |
| --- | --- | --- |
| Rimappatura e rilascio | 10/10 | ipi-service-996483a1576f4c30b8bf6ae730956347 |
| ACK mancante | 3/3 arresti attesi | ipi-service-0aea57d0a60e40bdb09800a1a3320bc1 |
| ACK di generazione errata | 3/3 arresti attesi | ipi-service-260d1ddc928748c38c9c16f580e696d8 |
| Boot ordinario | 10/10 | ap-runtime-fd3c0cd9433645ddb2206b0d61606dde |

Suite host ipi-service e layout passate con exit 0. Evidenze in
kernel/build/test-run-3af6ec2f0497450ea0ce7626a0333633 e
kernel/build/test-run-6d37d33b37e6424280cebb07bcaf0cf7.
Audit di emergenza passato per tutte e tre le immagini diagnostiche.
Resta da collegare il flush al paging ordinario e al rilascio dei
suoi frame e delle tabelle intermedie.

## Rilascio della page window

Il diagnostico rimappa e verifica i frame come sopra, poi chiama
x86_page_window_release sul frame sostitutivo. Il runner controlla
che ogni AP completi la richiesta 101 con EOI e ritorno da IRETQ,
e che l'inventario finale non contenga più nessuno dei due lease.

- 10/10 boot QEMU a 4 CPU con il rilascio della finestra.
  Evidenza: kernel/build/ipi-service-1aa85b633daf4d4eb6b28d46ac12e89c.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-08b9981f6101447f929d086ef0bc37de.
- Suite host ipi-service, kernel e layout superate. Log in
  kernel/build/test-run-9aa863b2c7fb4e85914b1ec00d5c47b6,
  kernel/build/test-run-52ac4958ff014c0f90a669f1ea5690f5 e
  kernel/build/test-run-5c616c8e82424dcbae68fe6284289db4.
- Audit del codice di emergenza passato sulle due immagini.

Gli arresti da ACK mancante o errato sopra sono prove della rimappatura
del blocco precedente. Non sono iniezioni nel rilascio finale.

## Rilascio delle regioni

Il diagnostico acquisisce una regione da due pagine dopo il test
della page window e ne verifica il rilascio. Per ciascun AP, la
richiesta 102 conferma il flush, l'EOI e il ritorno da IRETQ.
Il runner controlla che il lease fisico della regione sia assente
dall'inventario finale. Il PT dell'arena resta residente.

- 10/10 boot diagnostici QEMU a 4 CPU.
  Evidenza: kernel/build/ipi-service-bb279c97a26a40a0b3c7a015a4ebee15.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-29bfa67c01b64d1f9a66b933af176db4.
- Suite kernel, ipi-service e layout passate. Log in
  kernel/build/test-run-f73f2190f0834d3dbf11e4edba2ff90a,
  kernel/build/test-run-05cf54e0a9eb4720b535d5f6070293b6 e
  kernel/build/test-run-ad94eaa10167405d913067f88b9aa496.
- Audit di emergenza superato su entrambe le immagini.

Le prove di ACK mancante e di generazione errata sopra riguardano
la rimappatura diagnostica. Questa campagna copre il rilascio
positivo delle regioni.

## Rimozione del ramo heap

Il diagnostico positivo monta un binding temporaneo dopo l'ammissione
degli AP. Lo smonta senza utenti dell'heap. Verifica slot PML4 vuoto,
autorità rimossa e budget fisico ripristinato. Il runner legge
l'inventario e verifica che entrambe le lease siano state rilasciate.
La richiesta 103 completa flush, EOI e ritorno IRETQ su ogni AP.

- 10/10 boot diagnostici QEMU a 4 CPU.
  Evidenza: kernel/build/ipi-service-e1eb5debac4c4ff1973fe37541813f85.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-d52044afba554831814e10a493eee6a2.
- Suite kernel, ipi-service e layout superate. Log in
  kernel/build/test-run-3d5966fb1635490b9f0e2cfa4a7db584,
  kernel/build/test-run-d2db16b216c4417eb84a0c32beb71812 e
  kernel/build/test-run-90a1b98d9ae34b6081648576b613dca2.
- Audit di emergenza passato su immagine diagnostica e ordinaria.

Questo test verifica il teardown di un ramo heap senza utenti.
Le iniezioni ACK precedenti riguardano la rimappatura della finestra.

## Rimozione del ramo CPU dell'arena DMA

Il diagnostico crea un'arena di quattro pagine dati e tre tabelle
senza collegarla a un dispositivo. Il teardown scollega lo slot PML4,
attende gli AP e verifica il ripristino del budget. La richiesta 104
attesta flush, EOI e ritorno IRETQ su ogni AP. Il runner verifica che
le quattro lease siano assenti dall'inventario finale.

- 10/10 boot diagnostici QEMU a 4 CPU.
  Evidenza: kernel/build/ipi-service-125d0d2673e4410b89344918a1d70e6b.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-60333af0b11b460689353b61760c0eff.
- Suite dma-arena, ipi-service e layout superate. Log in
  kernel/build/test-run-e63338558d50401bb4c7a9ae9da1c919,
  kernel/build/test-run-07d2b92e1edd469d820c71b95d4a1bc1 e
  kernel/build/test-run-b6a9c6054eac4c528f8039a8894ee1d8.
- Audit di emergenza passato sulle due immagini.

Le prove isolano il ramo CPU. Il ciclo dispositivo e VT-d resta
responsabilità del chiamante e non è stato rieseguito in questa campagna.

## Rimozione del ramo framebuffer GOP

Il profilo positivo crea e rimuove un mapping temporaneo dopo l'avvio
degli AP. La richiesta 105 attesta flush, EOI e ritorno IRETQ su ogni
AP. Il runner verifica slot e lease delle tabelle, mentre il teardown
controlla il budget fisico. Il mapping riguarda la VRAM GOP e non
libera la memoria del dispositivo.

- 10/10 boot diagnostici QEMU a 4 CPU.
  Evidenza: kernel/build/ipi-service-dd2035d54354456c8d0254055e35ce4e.
- 10/10 boot ordinari QEMU a 4 CPU.
  Evidenza: kernel/build/ap-runtime-d68e78b60c404b4cbb43f6d7c9209c23.
- Suite x86-framebuffer, ipi-service e layout passate. Log in
  kernel/build/test-run-c2c44248543047198a0fdafd7fc6b329,
  kernel/build/test-run-2d645386f6b048df8c2c8b42652cc417 e
  kernel/build/test-run-f90480fe646d4154a5bdcb8bbfe5e7f2.
- Audit di emergenza passato sulle due immagini finali.

La prima immagine diagnostica ha raggiunto VGPU_MAPPING_FAILED:
VirtIO era ancora inizializzato dopo aver differito il mapping GOP.
La campagna finale separa l'avvio video ordinario dal profilo IPI e
riparte da zero. Evidenza iniziale:
kernel/build/ipi-service-8b44d81387e84b34af0f233b266bffb5.

## Rimozione dei mapping CPU VirtIO

I teardown dei registri PCI e della superficie staccano il ramo e
attendono gli AP prima di liberare le lease. Reset del dispositivo e
revoca VT-d restano responsabilità del chiamante.

- 10/10 boot QEMU a 4 CPU del profilo TestVgpuResources su VirtIO
  emulato, con reset, rilascio delle risorse e ripristino del budget.
  Evidenza: kernel/build/k4b-vgpu-resources-7fbb1ed9de3949b383b347556a02f7f2.
- Host mapping VirtIO, x86-vgpu-present e layout passati. Evidenze:
  kernel/build/k4b-vgpu-host,
  kernel/build/test-run-8e5bfe52361a44c282bf7328c1ba4186 e
  kernel/build/test-run-770fab1d24bc4a30b76dbeda8950b956.
- Audit di emergenza passato sulle immagini risorse e interattiva.

Il tentativo interattivo a quattro CPU ha completato il ciclo
bind/unbind/rebind dei registri e si è fermato a KERNEL_VGPU_IOMMU_BEGIN,
prima di avviare la console o raggiungere il teardown. La VM è stata
terminata manualmente e la sessione VT-d resta da verificare.
Evidenza: kernel/build/pointer-interactive-check-8060f506f06540cf8ee50df52a3e290e.

## Rilascio del mapping CPU VT-d

- 10/10 boot QEMU a quattro CPU, due cicli bind/unbind per boot
  con ripristino del budget e lettura coerente dei registri.
  Evidenza: kernel/build/k4b-vtd-registers-e6a3043142ef4c3b87a3cf82774dcbd2.
- Suite iommu-registers e layout passate. Evidenze:
  kernel/build/test-run-13c38bbc85814c958b2c53bd266f2c6b e
  kernel/build/test-run-edc65259aa06492aa2f01450c7cc71d7.
- Audit di emergenza passato.

Il profilo non attiva la traduzione del dispositivo. Nel profilo
interattivo, sette campioni HMP del BSP dopo KERNEL_VGPU_IOMMU_BEGIN
ricadono in verifiche ECAM, inventario e controlli di sovrapposizione.
Il BSP stava eseguendo codice prima del teardown. I campioni non
misurano la durata totale né escludono altri colli di bottiglia.
Evidenza: kernel/build/k4b-vtd-startup-sample-c10ace20f57e4a92adbda9c3a047d1cb/monitor.log.
La prova end-to-end è nella chiusura sotto.

## Chiusura K4-B

3 novembre 2026. K4-B è chiusa sul profilo QEMU xAPIC a quattro CPU.

**Sessione VirtIO/VT-d.** Il blocco precedente non era un difetto del
kernel: un boot dura circa 5,5 minuti sotto TCG e la VM era stata
fermata a mano. Inoltre la build del profilo superava il limite di
32.767 caratteri della riga di comando del linker; ora usa un
response file. Il runner verifica traduzione attivata e revocata
(trace `vtd_dmar_enable` 1 → 0), nessun fault della GPU, budget fisico
identico prima e dopo, e almeno uno shootdown confermato dagli AP
durante il teardown (`KERNEL_VGPU_SESSION_SHOOTDOWNS_*`).

**Barriera fallita in una vera API di rilascio.** Profilo
`release-missing-ack`: AP1 riceve il flush di `x86_page_window_release`,
completa EOI e non conferma. Il BSP va in panic coordinato. A VM ferma
il frame risulta ancora lease vivo, pinned una volta, con il contenuto
scritto prima del rilascio.

**Review dei percorsi di riuso.**

| Percorso | Barriera |
| --- | --- |
| Page window, regioni, heap, arena DMA, framebuffer, VirtIO, registri VT-d | Shootdown prima del rilascio |
| Stack dei task dello scheduler | Shootdown aggiunto ora: lo scheduler gira dopo l'avvio degli AP |
| Stack del kernel e stack principale | Locale: solo prima del commit e dello switch, prima degli AP |
| Risorse AP e tabelle di transizione | Locale: rollback solo se il gruppo non è trattenuto, prima del commit |
| Pagina SIPI | Mai rilasciata nel boot ordinario |
| Tabelle VT-d (dominio e coda) | CPU tramite page window; lato dispositivo servono reset GPU e invalidazione IOTLB |

I diagnostici IPI contano ora le generazioni dalla base trovata dalla
probe (`KERNEL_IPI_BASE_GENERATION`), perché gli shootdown del boot
consumano generazioni prima della probe.

**Campagna finale sull'immagine risultante.**

| Profilo QEMU | Esito | Evidenza in kernel/build |
| --- | --- | --- |
| Boot ordinario, 4 CPU | 10/10 | ap-runtime-ff4e8485ea6a40888acb5ed55aec116e |
| Diagnostico IPI con rimappatura | 10/10 | ipi-service-dc52577db8aa430bae3854d7f26b6a57 |
| Rilascio con ACK mancante | 10/10 arresti attesi | ipi-service-4bd8ad49de5e478bbf7b668af4ae7c86 |
| Console VirtIO/VT-d, 4 CPU, teardown | 10/10, 3 shootdown per boot | pointer-interactive-check-3487753778c7468e991226dd204828c2 |

Prima della campagna finale sono passati anche ACK mancante, ACK di
generazione errata, panic in idle e nell'handler, 2 e 8 CPU, panic
ordinario e boot a una CPU. Suite host: ipi-service, x86-scheduler,
scheduler, console, x86-vgpu-present, x86-virtio, pointer, ap-runtime e
layout. Coverage non misurata. Hardware fisico, x2APIC e PCID non provati.
