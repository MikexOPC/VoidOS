# Validazione delle protezioni di sistema

Versione completa precedente in `docs/handoff/security-plan.md` e `docs/design/k6-design.md` (K6-B), al commit `efecb8b`.

## Percorso ordinario e profili K6

- **SMEP, SMAP e UMIP.** Nel checkpoint del 12 novembre, due test host
  della policy, `k6-protection` 10/10 a quattro vCPU con lettura CR4
  su ciascuna e tre varianti senza SMEP e/o SMAP 3/3 ciascuna.
  `k6-process`, `k6-process-smp` e `k6-ordinary` 3/3 ciascuna.
  UMIP non era richiesto per ammettere ring 3. Restano non verificati
  il caso QEMU senza UMIP e il #GP di `SGDT` eseguito da ring 3.
- **Arresti con nome.** Dieci chiamate C ad arresti muti nel backend processi del checkpoint
  passano a panic identificabili. Le 18 campagne `k6-*` passarono
  tre boot ciascuna, negativi inclusi. La working tree di quella prova
  comprendeva lavoro GUI non committato.
  Gli arresti senza log di `x86_process_stop` restano in Assembly.
  Sono il ripiego quando GS o lo stato della transizione (stack e
  contesto) possono essere inaffidabili. Il ramo arresta la CPU senza
  chiamare il percorso di diagnostica C e lascia un marker in EAX.
  [Ramo Assembly](../../arch/x86/kernel/process/assembly/entry/transition.asm).
- **Guardie syscall.** Test Rust della geometria e 64 cicli C
  di acquisizione e rilascio a budget invariato. Campagne K6 18/18
  alla numerosità registrata nel checkpoint, con lavoro GUI
  non committato nella working tree della prova. Overflow reale
  dello stack non verificato.
- **Verifica documentale su `efecb8b`.** Build separata del kernel
  `ordinary`, un boot QEMU/OVMF a quattro vCPU. SMEP, SMAP e UMIP
  riletti su tutte e quattro. I risultati sul confine di privilegio
  e la copia utente sono nella [validazione dei processi](processi.md).

## Primitive host

| Modulo | Prove registrate |
| --- | --- |
| Bounds | 14 test debug e release, oracolo a 128 bit, clippy e oggetto freestanding. |
| Health test | 16 test, RCT e APT con cutoff confrontati con la tabella NIST. |
| SHA-256 | 9 test con vettori NIST e confronto con Windows CNG. |
| Memoria | 8 test e controllo dell'Assembly release per l'azzeramento. Tempo costante solo best effort. |
| HMAC | 8 test con RFC 4231 e confronto CNG. |
| HMAC_DRBG | 9 test con 480 vettori NIST CAVP. |
| Stima entropia | 19 test iniziali. Confronto non-IID su 105 valori di 7 dataset e restart H_r/H_c 28/28 contro NIST. Stima IID e accredito di jitter e IRQ restano aperti. |

Le prove host delle primitive non includono una sorgente collegata
all'avvio. Il kernel non usa ancora `lib/security`. Restano aperti
sorgente e credito di entropia, stack canary, W^X per sezione,
protezione degli input esterni e prova su PC fisico.
