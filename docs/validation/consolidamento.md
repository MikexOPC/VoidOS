# Validazione del consolidamento

Versione completa precedente in `docs/status.md`, `docs/review/verified-state.md`, `docs/review/status-2026-10-22.md`, `docs/handoff/codex-status.md`, `docs/handoff/k4-remaining.md`, `docs/design/k1-higher-half.md`, `docs/design/project-direction.md` e `docs/design/k6-design.md` (build), al commit `efecb8b`.

La validazione di chiusura dell'immagine unica è aperta. I conteggi
seguenti appartengono alle immagini e alle revisioni indicate nei
report. Lo [stato](../status.md) descrive l'immagine ordinaria attuale.

## Boot, memoria e dispositivi

| Checkpoint | Prova e perimetro |
| --- | --- |
| Loader, 2 ottobre | EFI di 54.272 byte, SHA-256 `FF6EB0D78AF37328394FE015171260577DB225E4CC23BC29996AF933C1AC10C1`. Test host C/C++ e Rust, 5000 mutazioni ELF e 512 sequenze di errore delle tabelle. QEMU/EDK2 10/10 casi loader. Il kernel ordinario non veniva eseguito e il cambio CR3 era fuori prova. |
| Dispatcher, 8 ottobre | 10 boot positivi e 9 negativi su QEMU TCG/OVMF. Scheduler cooperativo BSP, IRQ durante il worker, epoca e idle riusato. [Report](../../kernel/scheduler/idle/documentation/dispatcher-validation.md). |
| Boot e console, snapshot 22 ottobre | Bootstrap di memoria separato con 10 boot per profilo e 23 scenari diagnostici. Shell e input PS/2 in profili separati, console grafica GOP e prove VirtIO per componenti. [Bootstrap](../../kernel/bootstrap/documentation/memory.md), [PS/2](../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md), [shell](../../kernel/shell/documentation/shell.md), [presentazione VirtIO](../../kernel/drivers/video/virtio/resources/documentation/presentation-validation.md). |
| K1, revoca dello slot basso | Dieci boot dedicati e negativo con traduzione scaldata prima di MOV CR3. Kernel 23/23, campagne brevi 12/12, `v2c` 10/10 e 37 suite host complessive con retry mirato. Le pagine del loader restano riservate. [Report](../../arch/x86/mm/paging/retirement/documentation/validation.md). |
| K2, lock, pool e heap | Ticket lock con 140.000 acquisizioni FIFO host. Integrazione ordinaria 17/17 smoke, più dieci boot dell'ELF heap ricompilato. Contesa host con thread e ASan. Il runtime guest di questo checkpoint era BSP. [Report](../../kernel/sync/documentation/k2-integration.md). |

Per le verifiche puntuali dei driver del checkpoint di ottobre valgono
i report di [ECAM](../../arch/x86/kernel/drivers/bus/pci/ecam/documentation/validation.md),
[handoff PCI](../../arch/x86/boot/uefi/pci/documentation/validation.md),
[discovery VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/discovery/documentation/validation.md),
[domini VT-d](../../arch/x86/kernel/drivers/memory/iommu/intel/domains/documentation/validation.md)
e [code DMA VirtIO](../../kernel/drivers/video/virtio/queues/documentation/validation.md).

## CPU e scheduler

- **K3.** GS, clock delle attese, identità e risorse AP precedono
  ammissione, sigillo e avvio. K3-F aveva un solo core OS attivo anche
  con quattro vCPU configurate. K3-H provò la root inattiva e il
  rollback. K3-I avviò realmente BSP e tre AP e verificò il panic
  coordinato con origine BSP e AP. Chiusura 80/80 su QEMU/OVMF xAPIC,
  30 boot ordinari/console e 50 panic intenzionali, più una sessione
  continua di circa 301 s. [K3-F](#checkpoint-per-cpu),
  [K3-H](#checkpoint-per-cpu),
  [chiusura K3-I](../../arch/x86/kernel/cpu/percpu/documentation/k3i-runtime-validation.md).
- **K4-A/B.** PING agli AP nel boot ordinario, 40 boot su 1/2/4/8
  CPU. Shootdown prima del riuso dei frame, 40/40 a quattro CPU;
  ACK mancante con risorsa trattenuta nel caso diagnostico.
  [K4-A](../../arch/x86/kernel/cpu/percpu/documentation/k4a-validation.md),
  [K4-B](../../arch/x86/kernel/cpu/percpu/documentation/k4b-validation.md).
- **K4-C.** Il core Rust fu verificato in host e la preemption timer
  nei profili BSP/AP. `ap-task-four` provò otto task su quattro AP
  con BSP supervisore, 50 boot positivi e 20 arresti attesi per
  quiescenza incompleta. `v2c` passò 10/10 a quattro CPU e
  un soak VirtIO/VT-d di 301 s. Il comando `ap` resta della shell
  separata e i driver restano sul BSP.
  [Report K4-C](../../kernel/scheduler/documentation/k4c-validation.md).

## Build e runner

| Passo | Risultato storico |
| --- | --- |
| Pilota Meson | Su tre ripetizioni dell'editor di riga, mediana build+test 3,33 s PowerShell, 3,24 s Meson dopo setup e 2,17 s senza modifiche. Setup 7,82 s. Rebuild selettivo per C e Rust verificato. |
| M1, runner Rust | Parità sulla stessa immagine per `k5-copy` (30/30 per runner) e sei negativi (18/18 per runner). Coperti timeout, monitor, uscita e chiusura della VM posseduta. Dal registro, 7/7 campagne e 28 boot su immagini ricompilate. Windows, TCG/q35/OVMF. |
| M2, grafo Meson | Undici profili con stessi comandi, ordine e immagini byte per byte a parità di directory dopo `/Brepro`. Verificate invalidazione C, Rust e trampolino e nessuna ricompilazione senza modifiche. Quattro campagne Meson, 20 boot, più dieci boot singoli dei profili K6. |
| 12 novembre | A `efecb8b`, nove campagne ordinarie passano a Meson e 23 su 121 lo usano. Verifica documentale nel worktree con build separata: test host del loader completati, `plan_parity` 1/1, `ordinary` 1/1 con quattro vCPU e tre AP parcheggiati. |

Copertura, hardware fisico, build a un comando, CI e stress da trenta
minuti dell'immagine unica restano fuori dalle prove citate.

## Evidenze e limiti dei checkpoint

- Per il loader del 2 ottobre, i log erano nella build ignorata
  `firmware-test-9966cce985644e119bb26c4b8c259b44/`. Il preflight
  era verificato in QEMU, mentre ExitBootServices era provato con
  mock host. Il comportamento su alias di allocazione ambiguo
  trattiene la pagina per evitare di liberare la risorsa di un owner vivo.
- La validazione del 22 ottobre raccoglieva anche la correzione del
  rollover del timer, con 32 boot/sessioni finali e budget ripristinato.
  Per la console PS/2 riporta dieci boot shell, dieci ordinari e 612
  pressioni di stress. I dettagli e gli hash restano nei report dei
  moduli. [Rollover](../../arch/x86/kernel/timer/hpet/watchdog/documentation/idle/rollover.md),
  [verifica input](../../kernel/drivers/input/ps2/documentation/structure.md).
- Nei checkpoint K3-A–H, i casi host di GS, clock, identità, risorse e
  lock non provavano l'avvio di un AP. Il test di riserva bassa K3-G
  passò 10/10 dedicati, 10/10 ordinari, negativo 1/1 e console VirtIO/VT-d
  1/1. K3-H passò 10/10 preparazioni, rollback 1/1, 10/10 ordinari e
  10/10 console, ancora senza avviare AP. [K3-G](#checkpoint-per-cpu).
- Il pilota del planner misurò tre coppie di build pulite con mediane
  di 27,50 s PowerShell e 34,48 s con `-UseRustPlan`. Su nove prove
  del solo piano, 315 ms e 23 ms rispettivamente, con planner Rust già
  compilato. Sono misure sull'host del checkpoint.
- Il confronto M1 dei parser usò 5012 log (12 reali, gli altri mutati)
  senza differenze. La VM estranea rimase intatta nelle prove di
  chiusura. M2 verificò parità di piano, PE/ELF e layout. Restano
  dipendenze locali negli script e profili diagnostici legacy.
  [Guida strumenti e prove](../../scripts/README.md).

Le note operative storiche su pulizia degli artefatti e campagne
manuali restano nella [guida agli script](../../scripts/README.md).
Il [registro del debito](../future-work.md) raccoglie i limiti video
e IOMMU ancora da valutare.

## Checkpoint per CPU

I report completi di questi passaggi sono al commit `efecb8b`, in
`arch/x86/kernel/cpu/percpu/documentation/`. I risultati si riferiscono
alle immagini dei checkpoint. Nei casi host, ASan riguarda il C host.
La chiusura K3 nel report di integrazione prova il gruppo nel boot
ordinario. Le campagne dedicate provano i casi di panic.

| Passo | Esito e perimetro |
| --- | --- |
| K3-A | Contesto GS BSP promosso e contesto iniziale azzerato. 50 esecuzioni C/ASan, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Solo BSP operativo. |
| K3-B | Clock ACPI PM collegato alle attese dei lock. 50/50 esecuzioni host, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Clock di contesa AP fuori prova. |
| K3-C | Registro di boot con indice logico distinto da APIC ID e UID ACPI. 20 esecuzioni C/ASan, 10/10 diagnostici, 10/10 ordinari, due negativi e una console VirtIO/VT-d. Registro verificato dal monitor, AP fermi. |
| K3-D | Arena con lease dati e tabelle, stack e descrittori privati. Host con 101 punti di guasto, 64 geometrie e sweep di 63 descrittori. 10/10 diagnostici, 10/10 ordinari, due fault attesi e una console. Risorse predisposte, AP fermi. |
| K3-E | Contesti e attese privati, commit che impedisce il rilascio delle lease. 70 esecuzioni host del clock, 10/10 diagnostici, 10/10 ordinari e una console. Quattro vCPU configurate nel diagnostico, solo BSP attivo. |
| K3-F | Panic NMI, record privati, ACK e seriale d'emergenza. 21/21 scenari diagnostici, 10/10 ordinari e una console VirtIO/VT-d. Un solo core OS attivo, quindi la prova non copriva il panic remoto. |
| K3-G | Prima lease sotto 1 MiB, pinned prima delle allocazioni generiche. 10/10 dedicati, negativo senza lease 1/1, 10/10 ordinari e console VirtIO/VT-d 1/1. Nessun ingresso AP in questo checkpoint. |
| K3-H | Root condivisa ancora inattiva, codice RO/X, dati RW/NX e CR3 del BSP invariato. 10/10 preparazioni, rollback 1/1, 10/10 ordinari e 10/10 console. 19.478 confronti ASan host, nessun AP avviato. |
| K3-I, NMI isolato | 13/13 esperimenti SeaBIOS su BSP, dieci avvii consecutivi e tre controlli. NMI trattenuto nel cambio di modo e consegnato dopo IRETQ. Zero boot del kernel e zero AP. |
| K3-I, sigillo | Commit irreversibile delle lease di gruppo con risorse trattenute su errore. 53 scenari host, 10/10 UEFI del sigillo, due arresti intenzionali e 10/10 ordinari. Il sigillo non avviava gli AP. |
| K3-I, ritorno | Piano IRETQ e preparazione del panic dopo il sigillo. 17 esperimenti CPU isolati, 10/10 UEFI dedicati, 10/10 regressioni panic e 10/10 ordinari. AP fermi. |
| K3-I, ammissione | Corretto il controllo di alias e overflow degli stack di emergenza. 17 prove CPU isolate, 10/10 UEFI joint-panic e 10/10 ordinari. L'ammissione nativa non era ancora raggiunta da INIT/SIPI. |
| K3-I, pubblicazione gruppo | Descrittore unico pubblicato release/acquire dopo sigillo e preparazione panic. Cinque casi Rust debug/release, 20 processi host con quattro lettori, 10/10 dedicati e 10/10 ordinari. Nessun AP avviato. |
| K3-I, contesto AP | Binding ID/GS, stato atomico e catena alta compilata. 120 processi host per contesti/clock, 50 prove BSP e 20 identità, 17 prove CPU del ponte, 10/10 dedicati e 10/10 ordinari. I/O e identità erano modellati in host, AP fermi. |
| K3-I, primo AP | 10/10 con il primo AP realmente ammesso e parcheggiato e negativo senza NMI 1/1. Due regressioni 10/10 su sigillo e ordinaria con AP fermi. Contesti privati e registri reali verificati sul primo AP. |
| K3-I, panic remoto | 10/10 arresti provocati dal BSP con ACK del primo AP e 10/10 avvii senza panic. Origine su AP e intero gruppo furono oggetto del checkpoint successivo. |
| K3-I, avvio sequenziale | 10/10 boot diagnostici con BSP e tre AP realmente avviati, negativo di readback 1/1 e 31 scenari host. Regressioni primo AP e ordinaria 10/10 per configurazione provata, con AP fermi nelle immagini ordinarie del checkpoint. |
| K3-I, panic del gruppo | 10/10 per origine BSP e 10/10 per origine AP3, più regressione sequenziale 10/10. Un reporter e tre ACK su quattro core realmente avviati. Campagne dedicate distinte dalla successiva integrazione ordinaria. |

I nomi delle campagne storiche restano sotto per consentire a
`scripts/clean.ps1` di riconoscerne gli artefatti locali.

<details>
<summary>Identificativi delle prove K3</summary>

- k3 `percpu-firmware-5cae2ecfd3e84971a40367b9c7968655`, `percpu-firmware-6bb6fdd5fdf54ee2bb73f8273acc28ba`, `pointer-interactive-check-96f08a2f5af544ae942972f666e74755`, `qemu-run-b369823cddbc495d99db668e04e4846b`, `qemu-run-ee393c5e7bc645c6ad87484593559392`, `test-run-4920c69028d4412d8ef27217a87ab36d`, `test-run-56e90f1e63644415b16b7518868892eb`, `test-run-f2af482d2ed94918915dd1fe48ae52ac`
- k3b `percpu-firmware-9d1541a2fccd49f58f165b91a4f7ba35`, `percpu-firmware-ae4f1cf1547142e9b083ba0bbd5ea2c8`, `pointer-interactive-check-7cf4efe402164fa89613752310f19079`, `qemu-run-bf80adb8eea04616a8505921bd0e2412`, `qemu-run-f982587b2b994ebfa892039c231213ef`, `test-run-3d290516085c45b7b10516cefb60cd62`, `test-run-8e55ce489f16413f94ff785cce0df95b`, `test-run-9f4399eef94e4d6d82fbcc03894841fd`, `test-run-9f7d17942d894f71bfa143641d121e71`, `test-run-aa7ad9f112df4a1a9a8514e0921d39d2`
- k3c `percpu-firmware-b271326e2f83458c931daa6c52b35496`, `percpu-firmware-b3606ad6fa18453f964adf6160fb68cd`, `pointer-interactive-check-b268b50fc63e4c7f841541b8746f339a`, `qemu-run-5da9d43e7be641a1924075ebdbf3b9f9`, `qemu-run-788115aa8c1a445387a2b79159247eaf`, `test-run-21056bb6a13641369e0f6f60529af04b`, `test-run-6f81d88ccd014e9594ffed57cd4b24a7`, `test-run-8acb158cfa21472ca25dd9c756f47e8a`, `test-run-e4be5524070440a6827474067513d35f`
- k3d `percpu-firmware-0bbb26b5efa94aee9a086cc7cbdb582e`, `percpu-firmware-4fa7a6fc7223469297cacd46e071486d`, `pointer-interactive-check-b22bd13be6244e9bb679bd3c895e3923`, `qemu-run-7e40b16f1ce640d89d284d4a09915a53`, `qemu-run-83657883265941d0aff729d02efd7a9b`, `test-run-1098e164755c449a893ee4a347e98463`, `test-run-27694c11fe0544f9a1ff078c44051f42`, `test-run-55fcfa069760496788f3c5d04e66dac8`, `test-run-71b4e32a8c80444c8b2707be652ca444`, `test-run-7395cb040fb044ff8a4107745ee45df5`, `test-run-794bafd50d99457e9da4b03998776848`, `test-run-8ff187fff96a4ebeb7def76a0f9b9edd`, `test-run-a987f9804f8144bea5e50675914cf9e8`, `test-run-a9ef4c7e9fd248d7a7cef2cb64a18455`, `test-run-c0d751e9cdba44ddbaa2e8fbb6aa3573`, `test-run-e8d0505a17084788ad8391e8416a4755`
- k3e `ap-resources-host-54a1777d0ff84a67bad3e6800ed83ec8`, `percpu-clock-host-d2c8df15b8184d9ea1348a354df08687`, `percpu-firmware-1810b9910c0240c5b109f2ebde1e2604`, `percpu-firmware-5351fb11d18f4c76a7d8d876a5ec7025`, `percpu-host-188e07c34ad3431798c2a3a82c7fca9a`, `pointer-interactive-check-2f171d50d2de43efae2e648ae96c152c`
- k3f `panic-matrix-3876d94f5bf54ff594d3a09308536744`, `percpu-firmware-f4ed3b1cf7f44f7089a3e7580acd5176`, `percpu-panic-host-9d511360c7824fc288da6282c9e8e864`, `pointer-interactive-check-319ab4f5238c44648619ad422445f644`, `test-run-25788ca0329942b09b65c7dd2f78e65a`, `test-run-5171bcd6b0cd4689a8cf3fcce42c720c`, `test-run-56d3628a156d4d2cb93fb167651fc019`, `test-run-a64a6d354e7c43a183f46ec3b613ff32`
- k3g `ap-low-firmware-b632e3d8c9b14bd6a767c42be80ff94a`, `ap-low-firmware-c620ac32b7d545a182196fb84080c97d`, `ap-low-host-ba199844c65d43f683641485dc3c80ec`, `percpu-firmware-c0747f36619b4c8eacd11df3b68d2f32`, `pointer-interactive-check-1a063c9e81a8403f9fdc8aff037748ee`, `qemu-run-1397d68cd14b4600b545d4cb5cce3937`, `qemu-run-7e1ae78ee92d44ee8b5be48cdd6b4957`, `qemu-run-c4e61cf582a148a387240cc1a5557580`, `qemu-run-d03e52362deb4d8da95887f46d98e485`, `qemu-run-decb077c6cfd4fd68687296f0ab3b300`, `qemu-run-f3fb01e8d2ee4158a34f019af21ff84a`, `test-run-32855490f8aa4c7aac50b9a9b2ae9d14`, `test-run-72cabc8c51d44406b4dc9cfe6f1864c6`, `test-run-7f051ffa44e34555bc1ddaa6855b7865`, `test-run-a225c3ca589945c7af81d1ce80ed1ee6`, `test-run-a784220388704bb2aa4814aa6317ee7c`
- k3h `ap-low-host-1c59c51ea40d463f8a5ddb78a8746a51`, `ap-transition-firmware-05d603b7816b4220961a1dd51f4a8c06`, `ap-transition-firmware-1ff414fc308743f5b211d62bbeba5542`, `ap-transition-firmware-4c5641e76c0a48d68efb1d610dd594f6`, `ap-transition-firmware-68154817706c4f5494eba3c96f60b7d4`, `ap-transition-firmware-87dd8dc3d89a4e06ab4de9309d38f505`, `ap-transition-firmware-c03c767045064c2a99a867833af33267`, `ap-transition-host-c9243349fad943d495fda73e963e58cf`, `bootstrap-host-934fafe68f544833a7e44c6178b973e4`, `bootstrap-host-a846797862a74bd6a638ecd3712027ca`, `percpu-firmware-be6d1b5beda9446d84d1e28993ef7233`, `percpu-firmware-f3ffaae930964505ac71e99fea3d80d6`, `physical-host-6b874efe75224d38a14dd3479e8ce271`, `pointer-interactive-check-0296a0ad972744f88b53e4aefe355a5f`, `pointer-interactive-check-6daec293f5fe45bb921b375d43c5c2e8`, `pointer-interactive-check-b4068248bbca431b82cce4f109a76183`, `qemu-run-dbd2702d77b748049e3e0d6e851aae01`, `transition-boot-profile-a8904285b0bd43ddb4421a6ff5af0d24`
- k3i-nmi `ap-nmi-experiment-50dc6e2c6fe24a4e8e97267be41ab4c9`, `ap-startup-host-ff23a85c1b0845f1a134cd7b2a2a306a`
- k3i-joint `ap-joint-firmware-58aa4b1a8ef14feda8bed11706a1112a`, `ap-joint-firmware-632fd2ba764342b2bd325f3bce78ed5b`, `ap-joint-firmware-b505e53b90c6435c894d4af2e3395260`, `percpu-firmware-5c9ba72e408d4040a5b6d3b8b69bad88`
- k3i-return `ap-joint-firmware-5f19e1cb4f454812b3bf2633513d0d7d`, `ap-return-cpu-0471914cd9c3426b97e079cbd93a59f3`, `panic-firmware-80a01520004d4361886e01a0cb2d7820`, `percpu-firmware-a67292590bf94ec0abb29bad6d2a1261`
- k3i-admission `ap-joint-firmware-7aaa7c9041cc44d4a539db033e9b243e`, `ap-return-cpu-5bfd8fbe1ffa466b910fd91bcc4405ea`, `percpu-firmware-1eddaa0f4f654101bf772bba7eebd920`
- k3i-group `ap-joint-firmware-e41fd2e71238439181107f59ca0442e4`, `percpu-firmware-7c30dd19d0264d7a99a74bdfeeab2f20`
- k3i-context `ap-joint-firmware-2e35e759615742c68c3538cbbe098945`, `ap-return-cpu-f95373a1ce274d59bf816c7faa8d922d`, `percpu-firmware-7dd4c453872246dc988d30c207d88e5f`
- k3i-first-ap `ap-joint-firmware-bf56e06a8a5b4fe991e03e9f87c00990`, `first-ap-firmware-2da8dd3ab8c047029b7418bfbc812013`, `first-ap-firmware-451593f7f1dc420b9b53d04916dfa679`, `first-ap-firmware-f3faeb25785247fd932f301e15501cb4`, `percpu-firmware-565c54e793954df8902ac706246a7beb`
- k3i-remote-panic `first-ap-firmware-471f31f863264bf9b1ef4c261e952f14`, `first-ap-firmware-8ee8990917714751bbee910d66d65546`, `first-ap-firmware-d178ff203d1f4aab92c0d44a6f63fda1`, `first-ap-firmware-ee0714b54c53436786da304991fc08c7`, `percpu-firmware-6504f73f792e4a46877a719345f8831c`, `test-run-6d25a0e85d984aae9b3b0ec1de7674d2`
- k3i-sequential `first-ap-firmware-3587d31d5a04452698b3b9ff6c829623`, `first-ap-firmware-395f31bd4ea342969b05cd91192b0d8a`, `first-ap-firmware-636b446023964cc893651d3b23db5d24`, `percpu-firmware-046b5ac0b6954bf3aa89221a1d8662d0`, `percpu-firmware-a193a7be97ee45e3b99da44a810ebbed`, `percpu-firmware-da0835a5e0944f1ab60f29ce99a401a3`, `sequence-failure-35b8e7292a6c4755945d8e24ee89ab45`
- k3i-group-panic `first-ap-firmware-ef39759b5dc144d79dfa99ef1658b953`, `group-panic-804e51d110b64ae591e86840e26916cd`, `group-panic-a45b3079d2864c6f8e6356c9409b8c7f`

</details>
