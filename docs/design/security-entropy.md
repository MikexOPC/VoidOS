# Sorgente di entropia x86 e DRBG del kernel — progetto

Stato: progetto discusso e deciso (vedi Decisioni), nessun codice.
L'implementazione parte dopo la chiusura di K4-C5, perche tocca `arch/` e
l'avvio del kernel.

## Risultato atteso

All'avvio il kernel ottiene entropia dalla CPU, la valida con gli health
test e istanzia un HMAC_DRBG. Da li prendono valori casuali i canary
(passo 2 del piano) e le future randomizzazioni. Tutta la parte software
esiste gia in `lib/security/` (entropy, SHA-256, HMAC, DRBG).

## Fonti

| Fatto | Fonte |
|---|---|
| RDRAND presente se CPUID.01H:ECX[30] = 1 | Intel DRNG Software Implementation Guide §3.3.1 |
| RDSEED presente se CPUID.(EAX=07H,ECX=0):EBX[18] = 1; prima verificare che il leaf massimo sia ≥ 7 | Guida DRNG §3.3.2; R-003 |
| Successo solo con CF = 1; con CF = 0 la destinazione vale 0 e non va usata | Guida DRNG §5.2, §5.3 |
| RDRAND: ritentare 10 volte | Guida DRNG §5.2.1 |
| RDSEED: ritentare con PAUSE; se serve una risposta in tempo limitato, da 1 a 100 tentativi | Guida DRNG §5.3.1 |
| RDRAND esce da un CTR_DRBG che si riseeda da solo; RDSEED da una sorgente conforme a SP 800-90B/C, adatta a fare da seed per altri DRBG | Guida DRNG §3.2.3, §3.2.4 |
| Senza RDSEED: piu di 1022 RDRAND da 64 bit garantiscono un reseed interno | Guida DRNG §5.2.5 |
| Profilo QEMU del progetto (TCG, `-cpu max`, QEMU 11.1): `rdrand` e `rdseed` presenti | `query-cpu-model-expansion` via QMP, 2026-11-07 |

Prima di scrivere il codice vanno aggiunte le pagine RDRAND/RDSEED
dell'Intel SDM Vol. 2 (riferimento R-003), come chiede il Constraint 4.

## Alternative

1. **RDSEED come seed, RDRAND di riserva (proposta).** RDSEED e pensato per
   fare da seed a un DRBG software. Se manca o fallisce troppe volte, si usa
   RDRAND con sovracampionamento: 1024 valori da 64 bit compressi con
   SHA-256, cosi il DRBG della CPU si riseeda fra un seed e l'altro.
2. **Solo RDRAND.** Piu semplice, ma l'uscita e gia quella di un DRBG:
   l'entropia reale dipende dal riseed interno della CPU.
3. **CPU piu altre sorgenti (jitter del TSC, interrupt).** Piu robusta
   contro una CPU difettosa, ma richiede la stima dell'entropia di sorgenti
   nuove. Rinviata; il progetto resta aperto ad aggiungerla.

## Proposta v0

- **Cartella:** `arch/x86/security/entropy/`, test in
  `test/arch/x86/security/entropy/`.
- **Linguaggio:** Rust. CPUID, RDRAND e RDSEED si leggono con le intrinsic
  di `core::arch::x86_64` (`__cpuid_count`, `_rdseed64_step`,
  `_rdrand64_step`), senza assembly. Interfaccia C sottile solo per
  l'aggancio all'avvio.
- **Astrazione:** la sorgente CPU implementa un trait Rust; nei test host
  la si sostituisce con sorgenti finte (bloccata, sempre CF = 0, valori
  ripetuti, intermittente).
- **Health test** su ogni campione da 64 bit, prima di ogni uso (SP 800-90B
  §4.3): profilo non binario con H = 8 bit per campione (prudente rispetto
  ai 64 bit) e α = 2^-40, quindi RCT C = 6 e APT C = 19 su W = 512. Questi
  cutoff vengono dal calcolo gia verificato sulla Table 2. Un guasto e
  persistente.
- **Seed del DRBG:** scartare i 1024 campioni di avvio. Prima di istanziare
  o riseminare il DRBG, raccogliere un blocco con almeno 256 bit di
  min-entropia accreditata. Il parametro H dei controlli non accredita
  entropia: serve una stima che tenga conto delle dipendenze tra campioni.
  Se la stima consente di sommare 8 bit per ogni campione da 64 bit,
  servono almeno 32 campioni (256 byte grezzi). Per il nonce casuale
  accreditare separatamente almeno 128 bit. Vale lo stesso requisito di
  credito se si usa RDRAND come riserva.
- **Reseed:** ogni 2^16 richieste (politica piu severa del limite standard)
  e su richiesta.
- **SMP:** un solo DRBG globale con lock in v0. Per-CPU in seguito, se le
  misure lo chiedono.
- **Guasto all'avvio:** arresto con diagnostica (decisione 3).
- **Jitter:** campioni RDTSC non accreditati, mescolati nel DRBG.

## Decisioni (utente, 2026-11-07)

1. **Sorgente: alternativa 3, CPU + jitter + interrupt.** Per non dichiarare
   entropia non misurata:
   - **accreditata** (conta per i 256 bit del DRBG): solo la CPU, RDSEED
     con RDRAND di riserva come nell'alternativa 1;
   - **non accreditata** (mescolata come `personalization` e
     `additional_input`, che non possono peggiorare il DRBG): jitter dei
     tempi di esecuzione letto con RDTSC, e tempi degli interrupt;
   - **accreditamento futuro** di jitter/interrupt solo dopo la stima con
     gli estimatori non-IID di SP 800-90B §6.3 su hardware reale (strumento
     host in Rust da scrivere); sotto QEMU TCG il TSC e emulato e una stima
     non avrebbe valore.
   - Gli interrupt toccano i percorsi di K4: si collegano in un passo
     successivo, quando quei moduli sono chiusi.
2. **Linguaggio: Rust con intrinsic** (`core::arch::x86_64`), C solo per
   l'aggancio all'avvio.
3. **Guasto all'avvio: fermare il kernel con diagnostica.** Senza entropia
   CPU valida nessun canary o chiave parte con valori prevedibili; il
   jitter non accreditato non basta a proseguire.

## Ordine di lavoro

1. `arch/x86/security/entropy`: sorgente CPU, health test, seed del DRBG,
   arresto con diagnostica, jitter RDTSC non accreditato (dopo K4-C5).
2. Strumento host di stima SP 800-90B per i campioni di jitter (fatto: `test/tooling/entropy`).
3. Tempi degli interrupt come input non accreditato (dopo i moduli K4).
4. Canary del kernel (passo 2 del piano) sul DRBG.

## Criteri di accettazione

- Test host: CPUID assente o con bit spenti, CF = 0 continuo, valore
  bloccato e sorgente intermittente portano al comportamento deciso; i
  dati validi producono le stesse uscite del DRBG di riferimento.
- Test host: un blocco con credito insufficiente viene rifiutato anche
  se supera gli health test. Nonce, jitter e tempi IRQ non coprono
  il credito mancante per il seed.
- QEMU, 10 boot consecutivi (nuovo accesso CPU nel percorso di avvio):
  marker di entropia pronta, nessun fault inatteso.
- Test negativi QEMU: `-cpu max,-rdseed` usa RDRAND;
  `-cpu max,-rdrand,-rdseed` segue la decisione 3 con una diagnostica chiara.
