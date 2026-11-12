# Decisioni di progetto

**12 novembre 2026 — Una sola immagine.** Shell, SMP e processi utente devono convergere nell'immagine di produzione.
I test usano moduli separati, programmi utente o suite host.
La sequenza di chiusura e il cancello prima della GUI sono nella [roadmap](roadmap.md).

**12 novembre 2026 — Meson e Ninja per l'immagine ordinaria.** Le sue campagne compilano con Meson.
La build UEFI usa ancora lo script PowerShell esistente.
Il grafo Meson, il planner Rust e la parità sono nel [design della build](design/consolidamento.md#build-e-migrazione).

**12 novembre 2026 — Collocazione dei test.** I test rispecchiano i sorgenti in `test/kernel/`, `test/arch/x86/` e `test/lib/`.
Gli script stanno in `scripts/` e gli artefatti in `build/`.
I test del percorso ordinario usano moduli separati, programmi utente o suite host.

**12 novembre 2026 — Collocazione dei driver.** Driver comuni in `kernel/drivers/<categoria>/<dispositivo>/`.
Backend x86 in `arch/x86/kernel/drivers/<categoria>/<dispositivo>/`.
Le interfacce di architettura sono dichiarate in `kernel/`.

**12 novembre 2026 — Linguaggi e ABI.** C dominante, Rust per sicurezza, validazione, parser di input non fidati e GUI, Assembly per le operazioni CPU.
Il Rust del kernel è `no_std` e mantiene l'ABI Microsoft x64. Il target corrente è `x86_64-pc-windows-msvc`.
Un cambio di target del kernel richiede verifica preventiva. I programmi utente usano `x86_64-unknown-none`.

**12 novembre 2026 — Automazione e profili diagnostici.** Niente nuovi script PowerShell. Gli esistenti si migrano quando vengono toccati.
I profili disponibili solo in `build.ps1` si ritirano man mano che le prove passano a suite host o all'immagine unica.
Le campagne corrispondenti restano sul percorso legacy fino al ritiro.

**12 novembre 2026 — GUI e hardware.** La GUI segue i primi cinque obiettivi e il cancello «base solida».
USB con xHCI segue la GUI. Per ogni chiusura si dichiara la prova su PC fisico.
L'ordine dei passi è nella [roadmap](roadmap.md).

**Ottobre 2026 — Layout K1.** Migrazione a tappe dietro `arch/x86/configs/layout.h`, con immagine kernel alla base `0xFFFF_FFFF_8000_0000`.
I runner leggono l'header condiviso anche per verifiche precedenti al boot.
La revoca della metà bassa segue la verifica di stack e descrittori del kernel. [Design](design/consolidamento.md#layout-e-risorse).

**Ottobre 2026 — D2 e D4, GS e lock.** Il contesto runtime è per CPU e si raggiunge via GS.
Il lock usa ticket con salvataggio di IF, rango, rilascio LIFO e attese sorvegliate.
La contesa fra core richiede atomiche o lock. [Design](design/consolidamento.md#lock-cpu-e-scheduler).

**Novembre 2026 — D3-A, scheduler K4-C.** Coda globale protetta da lock e dispatcher per CPU.
La preemption timer richiede un frame di task interrompibile e assenza di lock tenuti.
Affinità fissa e driver BSP delimitano il percorso provato. [Validazione](validation/consolidamento.md#cpu-e-scheduler).

**Novembre 2026 — Primo processo incorporato.** K6 usa un ELF statico incorporato nell'immagine, con root utente dedicata.
SMEP e SMAP sono requisiti per ring 3. L'ingresso è `SYSCALL` e il ritorno passa da `IRETQ`.
L'esecuzione da file e la scelta su KPTI appartengono a obiettivi successivi. [Design](design/processi.md).

**7 novembre 2026 — Entropia del kernel.** Sorgente CPU con RDSEED e RDRAND di riserva, accesso da Rust con intrinsic.
Jitter RDTSC e tempi IRQ sono input supplementari senza credito di entropia.
All'avvio, se manca entropia valida, il kernel si ferma con diagnostica. [Design](design/sicurezza.md#entropia-e-canary).
