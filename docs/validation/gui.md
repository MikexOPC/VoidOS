# Validazione del desktop e delle applicazioni

## Stato corrente

**Parziale.** Il Terminale è collegato al dock. La campagna conclusiva della
prima task è in corso. Restano System Monitor con dati reali e la verifica
delle due app insieme. Hardware fisico non provato.

## Candidato della prima task

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
