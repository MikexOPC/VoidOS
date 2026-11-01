# K3-I — prova isolata del blocco NMI attraverso il cambio di modalità

30 ottobre 2026. Scelta A approvata dall'utente. Questo report verifica
un prerequisito CPU del [progetto NMI](k3i-nmi-design.md), non l'avvio AP
del kernel, il gate SMP o il boot ordinario UEFI.

## Immagine e ambiente

Fixture NASM in test/arch/x86/kernel/cpu/percpu/startup/assembly/nmi/.
Settore da 512 byte in disco di prova da 1 MiB, SeaBIOS, macchina pc,
una CPU max, TCG a singolo thread. Il runner possiede processo e disco;
snapshot=on, rete disabilitata, endpoint QMP/GDB solo su loopback, nessun
disco host o ESP del progetto. Alla fine termina e attende solo la VM
creata. NASM 3.01 verificato con hash, -Wall e warning come errori.

La fixture parte in modo reale sul BSP della VM; prepara una IVT privata,
stack, GDT e tabelle a quattro livelli con root fisica 0x10000. Il mapping
identità RWX dei primi 2 MiB serve soltanto a questo esperimento; non ha
ownership, guardie o NX del kernel e non è importabile come paging AP.
CPUID controlla MSR, PAE e long mode prima dei registri corrispondenti.

Fonti Intel SDM 253668-078US: §6.7/6.7.1 (NMI vero, blocco fino a IRET),
§10.7.1 (IVT), §10.8.5 (IA-32e), §4.1.2/4.5 (PAE, root, paging).
Le costanti di layout 0x5000, 0x7000 e 0x10000 sono del test, non offset
di registri di un dispositivo. QMP inject-nmi genera il vero evento;
non viene sostituito con INT 2.

## Risultati osservati

Immagine finale, campagna isolata:
kernel/build/ap-nmi-experiment-50dc6e2c6fe24a4e8e97267be41ab4c9/.
results.json: 13/13 casi, KernelBoots=0, ApStarted=0, ModuleReady=false.
Ogni sottocartella conserva argomenti, protocollo QMP/GDB, trace CPU,
stderr e snapshot dei registri ai confini; sector.lst conserva il listing.

- Dieci avvii consecutivi: NMI vero dopo IVT pronta, ingresso in modo
  protetto dentro l'handler, secondo NMI iniettato e trattenuto. Count=1
  resta stabile prima dell'abilitazione paging e dopo l'ingresso a 64 bit.
  Solo dopo IDT finale e IRETQ il conteggio raggiunge 2, con fase finale 5.
- Secondo NMI iniettato già in long mode ma prima di IRETQ: ancora bloccato,
  poi consegnato dopo il ritorno.
- Nessun secondo NMI: conteggio finale 1, senza conferma inventata.
- Nessun primo NMI: resta IVT pronta con conteggio zero, senza attraversare
  i modi autonomamente. Il runner conclude questo controllo e termina la VM.

Alla fase long mode: CS=0x18 con CS64, CR0=0x80010011, CR3=0x10000,
CR4=0x20, EFER=0x500. Questi ultimi bit attestano LME/LMA, non NXE.
Nessun check_exception o triple fault nel tratto di trace dopo IVT pronta
dei percorsi che completano la transizione. Il runner ha deadline di fase,
budget dei pacchetti/eventi e timeout socket; nessun PASS dal solo silenzio.
Le pause guest richiedono autorizzazioni esplicite del test host.

## Debug e limiti

Le prime prove hanno individuato warning NASM sulle relocazioni assolute
e default ABS implicito; corretti esplicitando indirizzi di caricamento e
DEFAULT ABS, mantenendo warning come errori. Il primo collegamento locale
QMP/GDB nel sandbox non era disponibile: tentativi scaduti conservati,
nessuna prova guest dichiarata PASS per quei tentativi. La campagna finale
è stata eseguita con accesso loopback fuori sandbox. Python usa un percorso
esplicito perché alias WindowsApps/launcher non erano utilizzabili lì.

Questa verifica sperimentale sostiene la sequenza di blocco NMI nel profilo
provato, non ne costituisce una dimostrazione per tutte le CPU o VM.
Non copre AP INIT/SIPI, NX/WP del kernel, IDT/TSS/IST/GS privati, token,
commit/lease o IRETQ verso il kernel alto. Non prova NMI ricevuti prima
dell'installazione della IVT; rimane il vincolo di piattaforma del design.
Un IRET faulting sblocca comunque gli NMI: il frame reale richiede controlli
e fault handling prima dell'integrazione. Nessun ASan/Valgrind applicabile
alla fixture Assembly; nessuna deduzione su leak fisici del kernel.

## Riproduzione

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File arch/x86/kernel/cpu/percpu/startup/scripts/test-nmi.ps1 -Runs 10 -Python C:/Users/Mike/AppData/Local/Programs/Python/Python312/python.exe

Dipendenza locale: assembler.ps1 -Download installa soltanto in build/;
senza -Download si limita a verificare gli eseguibili. Hash vincolati ai
byte del pacchetto ufficiale HTTPS, non a una firma indipendente.
La prova richiede che QMP/GDB loopback siano consentiti.

Regressione host finale del coordinatore/record:
kernel/build/ap-startup-host-ff23a85c1b0845f1a134cd7b2a2a306a/,
16/16 casi Rust in debug e release, due eseguibili C/Rust con ASan sul C,
cppcheck passato. Controllati limiti sorgenti/cartelle, whitespace e link
documentali locali. Nessuna nuova prova del kernel UEFI conteggiata.
Lo script host termina ora esplicitamente con exit 0 quando eseguito come
file, come le altre suite per-CPU: verifica finale completata con codice 0.
