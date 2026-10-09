# Mapping framebuffer x86 v0

Modulo: `arch/x86/kernel/drivers/video/framebuffer/`. Il backend mappa
il descrittore GOP gia validato; non gestisce registri GPU, DMA o cambio
modo video. La scelta autorizzata e UC, un framebuffer oltre al LAPIC,
tabelle kernel da lease pinned e rollback. Il rendering e separato.

## Contratto

Profilo BSP, quattro livelli, IF=0, PCID disabilitato, nessun altro writer
delle tabelle. Nessun AP e avviato; non e un protocollo di shootdown SMP.
Owner persistente azzerato, fuori da input, output e viste speciali.
Descrittore assente: successo senza mapping o allocazioni. Presente:
validazione del descrittore e dell'intera Capacity, esclusione di RAM,
risorse vive, tabelle e alias esistenti prima delle scritture.

Il ramo PML4[225] parte vuoto. La prima pagina e una guardia; il primo
byte della pagina mappata e a `(225 << 39) + 4096`. Per i pixel bisognera
aggiungere `Base & 4095`. Vengono mappate solo le pagine di Used, non tutta
Capacity; anche la guardia successiva e tutte le entry rimanenti sono zero.
Massimo 4321 pagine video e 11 pagine di tabelle; tabelle non globali,
supervisor, foglie RW/NX e UC forte da PAT esistente. Nessuna modifica PAT
o MTRR. Le tabelle RAM richiedono PA0 WB e non devono avere alias dati.

C possiede lease, pin e transazioni; Rust costruisce le singole entry senza
puntatori o unsafe; il backend nativo legge i prerequisiti CPU e ricarica
lo stesso CR3. Le tabelle detached vengono inizializzate e verificate usando
il prestito temporaneo esistente, revocato prima di pubblicare il ramo.
La fotografia finale usa il contratto dell'inventario video e conserva ID
dei lease, non soltanto indirizzi riutilizzabili.

Unbind verifica owner e CPU, rimuove solo il ramo posseduto, invalida le
traduzioni e ricattura l'inventario LAPIC prima di togliere pin e lease.
Il budget iniziale deve essere ripristinato prima di azzerare l'owner.
Errore di revoca/invalidation/ownership: risorse trattenute e fallimento,
cleanup ritentabile dopo riparazione del prerequisito. Il chiamante kernel
fa panic se bind fallisce; non continua con un mapping ambiguo.

## Fonti hardware

[Intel SDM Vol.3A, 253668-090US, febbraio2026](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):

- §2.3 e §2.5: RFLAGS.IF bit9 spento, bit1 impostato; CR0.PE0/WP16/PG31
  accesi, NW29/CD30 spenti; CR4.PAE5 acceso, LA5712 e PCIDE17 spenti.
  Questi sono prerequisiti, non protezioni abilitate indiscriminatamente.
- §6.13, tabella6-4 e §6.13.3: IA32_EFER 0xC0000080, LMA10/NXE11
  accesi, NX da CPUID.80000001H:EDX20; MAXPHYADDR da
  CPUID.80000008H:EAX[7:0], limitato al profilo32..52.
- §2.8.7, MSR, e §14.12.1: CPUID.1:EDX5 per MSR, EDX16 per PAT;
  verificare le foglie massime prima delle query e il supporto prima RDMSR.
- §5.5.2, tabella5-20: PTE4KiB P0/RW1, U/S2=0, XD63=1; nessun bit
  Global8. A5/D6 possono essere aggiornati dalla CPU e sono ignorati nel
  confronto della fotografia. Gli indirizzi rispettano MAXPHYADDR.
- §14.12.2/.3, tabelle14-10/.11: IA32_PAT 0x277; UC0, WB6;
  indice PAT7:PCD4:PWT3. Il helper esistente uc_slot ritorna indice+1;
  sottrarre1 prima di codificare la PTE. La v0 non programma la PAT.
- §5.10.4.1: MOV allo stesso CR3, con PCIDE=0, invalida traduzioni non
  globali e cache delle strutture paging. Serve anche per le viste ricorsive
  delle tabelle rimosse: INVLPG dei soli pixel non sarebbe sufficiente.

Per l'indirizzo di IA32_EFER: [Intel SDM Vol.4,335592-071US,ottobre2019,
tabella2-2](https://www.intel.com/content/dam/develop/external/us/en/documents/335592-sdm-vol-4.pdf),
voce C000_0080H, LMA10 e NXE11. E una fonte architetturale storica;
la disponibilita viene comunque verificata tramite CPUID a runtime.

## Prove e limiti

`scripts/test.ps1`: tre casi Rust debug/release, Clippy; sette gruppi C
con allocator reale e backend CPU/mapping simulato ripetuti100 volte,
MSVC /W4 /WX, ASan e cppcheck. Esaurimento dei lease, borrow interrotto,
revoca temporanea persa, invalidazione fallita, owner copiato, PAT mutata,
ramo estraneo, foglia user, snapshot con ID vecchi, alias RAM e limiti massimi.
Il budget e ripristinato dopo cleanup; risorse restano pinned nei fallimenti
che impediscono una revoca sicura. ASan strumenta C host, non Rust o RAM
fisica. Valgrind non disponibile sulla toolchain Windows: non e un PASS.

`scripts/firmware.py`: VM possedute con deadline, ESP e hash verificati,
ispezione fisica indipendente di PDPT/PD/PT, ogni foglia e guardia, PAT
selezionata e CR3. `-TestFramebufferMapping` esegue unbind, verifica budget
e ricattura LAPIC, poi rebind. L'immagine ordinaria conserva il mapping e
i lease pinned a KERNEL_READY per il successivo servizio video.
L'assenza video conserva il percorso seriale. Gli errori di revoca sono
iniettati host, non provocati nel firmware reale. Pixel, WC, hardware fisico,
SMP e prestazioni non sono ancora validati da questo blocco; coverage N/D.

La scansione anti-alias e bounded a 11 lease,128 tabelle e512 entry per PT.
E un costo di bind/unbind, non un percorso per pixel; nessun benchmark
di rendering e dichiarato.
