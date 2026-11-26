# Validazione del desktop e delle applicazioni

## Stato corrente

**Parziale.** Il Terminale è collegato al dock. La campagna conclusiva della
prima task è in corso. Restano System Monitor con dati reali e la verifica
delle due app insieme. Hardware fisico non provato.

## Candidato della prima task

### Chiusura input GUI, cursore e budget WHPX — 26 novembre 2026

**Fatta.** Chiuse le modifiche su cursore (guadagno 3/2 con resto per asse,
attesa primo frame, serializzazione 18×18 con il present), tastiera GUI
(rilasci/modificatori/resync, `applications.has_pending()` nel compositor per
non dormire in `gui_wait` con tasti in coda) e budget rinnovabile WHPX
(`cpu->RenewBudget`: i processi con `renew_budget = 1` non vengono uccisi con
`ExitKind=4` al superamento di un singolo turno/syscall lunga).

- **Test host (`meson test -C kernel/build/meson-host-tests`):** 32/32 PASS
  (inclusi `pointer_sensitivity`, `gui_host`, `libui_rust`, `sync_core`,
  `process_table_host`, `heap_host`).
- **Campagna GUI TCG (`qemu-runner-18eba12ebc2681e0-2c48`):** q35 TCG 4 vCPU,
  Kernel SHA-256 `8C00E117C28D440C180B516722E0259BDD2EF91F99217982DA81C2682AB64828`,
  3/3 boot PASS (70 s ciascuno, 21 cicli totali, 42 avvii Terminale, 21 comandi
  VFS con Shift/Backspace/lettere ripetute, 0 pixel residui nel controllo
  `cursor_trails`).
- **Regressione `ordinary-shell` TCG (`qemu-runner-18eba16bd80fadb8-d3c`):**
  stesso kernel, 4 vCPU, 10/10 boot PASS.
- **Boot WHPX headless (`whpx,kernel-irqchip=off`, 4 vCPU):** 5/5 boot PASS
  fino a `SHELL_TASK2_READY` (~4,9–5,0 s/boot, `KERNEL_VFS_EXEC_OK` e
  `KERNEL_USER_SHELL_OK` superati). Fluidità WHPX/SDL interattiva e hardware
  fisico restano da provare a mano.

### Sensibilita e residui del cursore — 25 novembre 2026

**Parziale, modifiche locali senza commit.** Guadagno 3/2 con resto per asse.
Il present sospende il disegno del cursore e poi usa l'ultima posizione
richiesta. Ripristino, composizione e commit dell'area 18×18 sono atomici
rispetto alla preemption BSP. Il puntatore attende il primo frame completo.

Build kernel e runner PASS. Test host `pointer_sensitivity`, `gui_host` e
quattro casi `libui_rust` PASS. Il test della sensibilita verifica piccoli
movimenti, inversioni e accumulo per delta da -256 a 255.

Campagna `kernel/build/qemu-runner-18eb5eddb14dc744-5584`, q35 TCG con quattro
vCPU, 1/1 boot GUI PASS, 63 secondi, sei cicli e dodici avvii di Terminale.
Kernel SHA-256 `D0588E3811F4E499DADE07615FB31D04BCEA0F5C4D490307522B84AEA6FB451B`.
Il confronto fra `cursor-before.ppm` e `cursor-after.ppm`, dopo movimento
continuo e passaggi sul dock, trova zero pixel modificati nell'area desktop
escludendo le posizioni iniziale/finale del cursore e gli ultimi 100 pixel
verticali. La prova non copre ogni interleaving o la fluidita percepita.

Campagna ordinary-shell `kernel/build/qemu-runner-18eb5f16e6bb6740-85cc`,
stesso kernel e quattro vCPU TCG, 10/10 boot PASS. Processi, bridge,
preemption BSP/AP e dispatcher verificati dal runner; zero overruns.
Il comando Meson dei test host si e bloccato ed e stato terminato. Gli
eseguibili host indicati sopra sono stati eseguiti direttamente con il
runtime ASan nel PATH, tutti con uscita zero.

WHPX/SDL fallisce prima della GUI nei tentativi
`mouse-probe-1795633462619318400` e `mouse-probe-1795633553752572900`.
`VFS_EXEC_RUN_FAILED`, uscita per budget (`ExitKind=4`). Il controllo con il
kernel precedente di `desktop-18eb5e3febb7cf90` fallisce allo stesso punto
in `mouse-probe-1795633594537818000`. Questo confronto riproduce il blocco
anche prima delle modifiche al cursore. Fluidita WHPX e hardware fisico
restano da verificare.

### Tastiera GUI — 25 novembre 2026

**Parziale, modifiche locali senza commit.** Inoltro di pressione, rilascio,
modificatori e reset dopo errore del flusso. Il Terminale ignora i rilasci e
i report senza testo. Logging e annullamento della riga della shell esclusi
durante l'input GUI. Il runner verifica Shift, lettere ripetute e Backspace.

Test C/Rust del consumer GUI con ASan PASS per Shift+A, rilasci, ripetizione,
Esc e reset. Coda GUI e scheduler sono simulati nel test. Consumer console
con ASan PASS nei 277 scenari esistenti. Oggetto Rust ricompilato dai sorgenti
attuali in `kernel/build/input-audit/decoder.obj`. Test host GUI PASS anche
per il risveglio e la consegna del reset esplicito.

Campagna `kernel/build/qemu-runner-18eb5a90fd071ee8-92ec/`, ordinary q35 TCG
con quattro vCPU: 1 boot PASS, 1 fallito, terzo non eseguito. Nel secondo
boot manca `VoidOS terminal command processed (3)`, senza causa isolata.
Questa campagna non chiude la task e non dimostra fluidita interattiva.

### Puntatore coerente — 25 novembre 2026

**Fatta.** Posizione autorevole nel kernel, coordinate assolute nella stessa
ABI input da 24 byte e guadagno 2x comune a cursore e click. Ogni report
applica i bordi prima dell'accorpamento. Il test host GUI passa con casi
di inversione al bordo, delta estremi, pulsanti e recupero overflow.

Campagna `kernel/build/qemu-runner-18eb59e66ff77a20-8630/`, ordinary q35 TCG
con quattro vCPU, 3/3 boot GUI PASS. Kernel SHA-256
`5E156CDCEFCEC7080F6776A9F453B60D0CFB2529C617300E441194880D0F513C` e UI
`5F9F889F0F98BAF3A0DFFCC9DF30273850B4057BA9D5DB7200BAEE68014BA01E`.
La build kernel e il test host sono stati compilati con Meson. Il wrapper
PowerShell della UI e rimasto senza esito; Cargo offline e copia esplicita
dell'ELF hanno completato la preparazione dell'immagine prima dei boot.
Il miglioramento della fluidita nella VM interattiva dell'utente e lo
stress da 30 minuti restano da verificare. Nessuna prova su hardware fisico.

### Correzione del primo click — 24 novembre 2026

**Fatta.** La UI viene inizializzata dall'elenco reale delle applicazioni
prima di essere mostrata. Il primo click sul dock avvia il Terminale.
In precedenza `terminal-open` era vero per l'anteprima, anche senza processo,
e il primo click chiudeva soltanto quella finestra.

Il runner valida il payload PPM usando le dimensioni del file e seleziona
i controlli del dock attuale. I movimenti sono limitati a 100 per asse.
La geometria dei controlli resta quella del layout corrente.

Immagine `ordinary`, QEMU/OVMF q35 TCG, quattro vCPU, 1280×720.
Kernel SHA-256 `4C6E2900E2C285FC4B2AB1F8BDBF03CADC1E90BD07C732F4C1606E48C92BDE94`.
UI SHA-256 `C51605252F1CCB7F760A18445F12B2EE2FBFA6A6BF4C08774CD4C15E3E8E4F13`.

- 3/3 boot GUI PASS, 21 cicli, 42 avvii del Terminale e 21 comandi VFS.
  Le sessioni GUI durano 69, 68 e 70 secondi. Verificati chiusura,
  riapertura, ritorno alla shell e `bridge status`.
  Evidenze `kernel/build/qemu-runner-18eb0d2e419e47a8-5c88/` e
  `kernel/build/qemu-runner-18eb0d446ee4d0f4-8598/`.
- Runner host 22/22 test unitari PASS e 8/8 test con VM simulata PASS.
  Questi ultimi verificano il runner. Il percorso OS è verificato dai boot.
- Screenshot del comando nel Terminale reale ispezionato visivamente.
  Hardware fisico, stress da 30 minuti e latenza ingresso-pixel non provati.

Le precedenti campagne fallite si fermavano sul formato PPM fisso o sul
primo click. Non contribuiscono ai conteggi PASS.

### Candidato storico

Immagine `ordinary`, QEMU/OVMF q35 TCG, 128 MiB, quattro vCPU. Tutti gli ELF
sono congelati in `kernel/build/desktop-terminal-candidate1/`; gli hash
completi sono nel relativo `hashes.json`.

| Binario | SHA-256 |
| --- | --- |
| kernel.elf | `7EC8457E4B8E81EE7BE54D244709FB050428DA2034D7F9D1644C434284A4AD3B` |
| user-ui.elf | `F771BFDC15197B56E4B5856ED6F5F76549676E6012C6E9B0EFE5E3C6B66F46BE` |
| user-init.elf | `D45FB995467AF9AC63E2C49B88B864466BF7F4AD7CA875BD296F4D2AAC7CE4BC` |
| user-terminal.elf | `1F977C72F66CEEBD9ACC7035054B96295A3303BCD46B819000167376053C25CC` |

## Prove eseguite

- **10/10 boot consecutivi PASS** sul candidato congelato.
  `kernel/build/desktop-terminal-candidate1-boots10/`.
  Ogni boot avvia il Terminale dal compositor, riceve create e commit reali,
  invia un comando, compone la superficie e recupera le risorse. Una seconda
  istanza del compositor termina con un figlio ancora attivo; la raccolta
  del padre deve ripristinare il budget fisico. Le prove ordinarie includono
  anche processi, IPC BSP/AP, shell e continuità del dispatcher dopo `exit`.
- Il tentativo di ricevere un messaggio IPC nella superficie condivisa
  read-only restituisce errore di copia. La ricezione successiva nel buffer
  scrivibile conserva il messaggio. Il Terminale deve vedersi negare il
  mapping del backbuffer del compositor.
- **7/7 suite host PASS**, `desktop-terminal-host-final.log`.
  `gui_host`, `process_table_host`, `process_policy_rust`, `shell_command_rust`,
  `libvoid_rust`, `libui_rust`, `clock`. ASan sui test C dove configurato.
  Includono autorizzazione SHM, pin durante unmap, retry, crescita e cleanup,
  overflow dei range, finestre SHM per user-copy e wrap del clock.
- Rendering Slint host e callback del launcher PASS,
  `desktop-connected-render.log`. Il clock e la piattaforma sono fixture host.
  Questa prova verifica rendering e richieste UI; l'integrazione OS viene
  verificata in QEMU.
- Runner **2/2 suite PASS**, `desktop-runner-tests.log`; piano di build
  `plan_parity` PASS, `desktop-terminal-plan.log`.

## Correzioni emerse

La prima prova visiva forzava con GDB il modo interattivo dentro il self-check
prima dell'inizializzazione PS/2. L'ingresso ordinario `gui` elimina quella
modalità di prova. Sono stati corretti l'autorizzazione SHM, il rollback
parziale delle PTE, le copie syscall dall'heap SHM e il wrap dell'uptime.

Una campagna è stata invalidata da una mia ricompilazione durante i boot.
Dopo otto successi il nono boot ha mescolato init/kernel e UI di revisioni
diverse. Quella campagna non vale per la chiusura. La campagna da 10/10 usa
il candidato congelato sopra. Un link del runner è stato ritentato dopo la
chiusura dell'eseguibile che Windows teneva occupato.

Il checkpoint `329392a` provava rendering e self-check delle app separate.
Le sue prove non dimostravano il dock collegato ai processi applicativi.
Il design attuale è in [gui.md](../design/gui.md).
