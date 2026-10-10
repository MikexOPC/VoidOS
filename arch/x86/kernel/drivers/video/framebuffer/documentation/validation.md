# Mapping live — verifica del10 ottobre2026

Snapshot del blocco mapping precedente alla scrittura pixel. Stato corrente
e nuove prove nel [report pixel](pixels.md); le immagini sotto restano
quelle della campagna del mapper.

Modulo circoscritto al backend framebuffer x86; wiring del kernel e hook
opzionale nel provider QEMU esistente. `reuse/` intatto. Nessun nuovo driver
GPU, rendering o SMP. [Contratto e fonti](mapping.md).

## Problemi trovati e corretti

- Il helper PAT ritorna indice+1: il primo boot falliva nel preflight per
  conversione errata. Sottrarre1 prima della codifica, verificare la policy
  Rust e indipendentemente il valore PAT selezionato nel monitor.
- Prima di scrivere tabelle detached, verificare che nessuna pagina abbia
  un alias dati nel grafo corrente; il test con RAM libera gia mappata
  richiede rifiuto prima di qualsiasi borrow e budget ripristinato.
- Firma del mock writable corretta a int, come l'interfaccia pubblica;
  warning MSVC trattato come errore. Test Rust nel percorso test/rust/tables.
- Prerequisiti nativi includono PAE e LMA/NXE; ambiente eccezioni verificato
  prima di allocare. Nessun fallback che ignori un prerequisito mancante.

## Host

`kernel/build/framebuffer-owned-host.log` e directory omonima:
3/3 casi Rust in debug e3/3 in release, Clippy con warning come errori.
Sette gruppi C ripetuti100 volte, MSVC /W4 /WX, ASan; include esaurimento
dei64 slot lease, ownership copiata, snapshot con ID vecchi, PAT modificata,
ramo estraneo, foglia user, aperture RAM, alias delle nuove tabelle,
4321 pagine video/11 tabelle, borrow perso e flush fallito.
Budget fixture128:125 con3 tabelle,117 con11; ritorna128 dopo unbind.
Nei fallimenti non revocabili le pagine restano pinned e non riutilizzabili;
cleanup dopo riparazione del mock ripristina il budget.
Cppcheck controlla8 translation unit: nessuna diagnostica error/warning/
performance/portability. Le note style/information del set parziale e dei
header di sistema non sono warning del compilatore o difetti hardware.
Suite host kernel completa: `kernel/build/framebuffer-live-kernel-host.log`.

ASan strumenta C host, non il Rust linkato o la RAM fisica QEMU. Accounting
dei lease e budget verificato separatamente. Valgrind non disponibile
su Windows; non dichiarato eseguito. Coverage linee/branch N/D.

## Firmware finale

Loader SHA256:
`EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7`.
Kernel diagnostico:
`4053FEC296FD58F322F442F88C762A2F052D15F5C450C2EBB2853BC9B30227D0`.
Kernel ordinario:
`5DC658F9A3F5272324854F1ACD60D755EFEF81BA95918A95F13700040014D63F`.
Immagine ordinaria copiata nella ESP soltanto dopo le30 prove finali;
hash della copia confrontato. Backup sotto kernel/build/:
`framebuffer-live-previous-8db96ba2de4f49fab7f6fdda24c90526.elf`.

QEMU q35/TCG, CPU max,128MiB, BSP, OVMF. ESP copiata e hash confrontati;
deadline, solo VM possedute terminate. Tutti raggiungono KERNEL_READY,
nessun FAIL o triple fault inatteso, registri acquisiti e loader invariato.

| Serie | Boot consecutivi | Evidenza sotto kernel/build/ |
|---|---:|---|
| Diagnostico, unbind/budget/rebind |10/10|framebuffer-live-8dca5f4c48564e409a041c2a73336144/results.json|
| Ordinario, mapping video live |10/10|framebuffer-live-f4baedc6ecf34c47b0f3b89a7f34cb80/results.json|
| Ordinario, display assente |10/10|framebuffer-live-6cbcb496240c493192c3c14bdc90798a/results.json|

1024x768 BGR,768 pagine video e4 tabelle owned. Il monitor fermando la VM
legge fisicamente root, PDPT, PD e tutte le PT:768 foglie con fisico esatto,
RW/NX/supervisor/UC, guardie zero, nessuna entry extra o tabella duplicata;
PAT indice3 UC0, PA0 WB6; CR3 invariato. Budget live=budget prima-4.
Nel diagnostico il kernel verifica unbind e budget completo prima del
rebind. Nel profilo assente nessun owner o mapping live viene pubblicato.
Screenshot solo come verifica della geometria GOP: PixelValidated=false,
non e una prova di disegno dal kernel. Errori di revoca iniettati solo host.

## Review finale

`kernel/build/framebuffer-live-review.json`: manifest SHA256 dei sorgenti,
limiti, separatori, whitespace e link locali; nessun errore. Nessun file
sorgente oltre400 righe, massimo5 sorgenti diretti nella cartella funzionale.
Il mapping ordinario rimane owned/pinned a KERNEL_READY: durata persistente
intenzionale per il servizio video, non risorse anonime da liberare.
Rendering, write-combining, hardware fisico e shootdown SMP sono gate futuri.
