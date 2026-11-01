# K3-I — prefisso alto privato, 1 novembre 2026

Parte dell'ingresso diagnostico già concordato in [k3i-design.md](k3i-design.md)
e del protocollo [NMI approvato](k3i-nmi-design.md). Non avvia AP e non è
collegato alla build del kernel. È il tratto fra il salto alto del blob e
la futura ammissione al runtime; non sostituisce il commit congiunto.

## Sequenza e motivo

L'ingresso Assembly riceve RCX = pagina dati bassa e RDX = token, con IF=0,
NMI bloccato e CR3 transitorio. Il chiamante deve aver verificato il record
C/Rust, lease, mapping, capacità CPU, identità, generazione e pubblicazione.
Gli indirizzi sono memoria kernel fidata, non puntatori utente. I controlli
iniziali versione/dimensione/token non sostituiscono quelle verifiche.

1. Riserva 1024 byte nello stack alto privato, li azzera e copia i 104 byte
   del record. Il resto dello stack deve restare mappato per transizioni e
   fault; non basta mappare solo il frame.
2. Costruisce IDT terminale e GDT intermedia nello stesso frame. Il codice
   64 bit del blob usa 0x18, mentre 0x18 nella GDT privata identifica la TSS.
   La GDT intermedia contiene codice 64 bit sia a 0x08 sia a 0x18, consentendo
   il passaggio con RETFQ e la conversione delle gate a 0x08. Non usa IRET.
3. Installa CR3 operativo solo dopo aver portato stack, IDT e GDT in alto.
   Controlla la rilettura di CR3; i vecchi indirizzi bassi non servono più.
4. Carica GDT privata, segmenti dati e TSS tramite LTR 0x18. Installa la base
   GS dal record, inizializza controlli x87/MXCSR e azzera i sedici XMM.
5. Salta alla continuazione Assembly `xs_private_arrive`, con RCX e R8 al
   frame, RDX al token, R15 stabile al frame e RSP al suo inizio, allineato
   a 16 byte. RSI/RDI non conservano puntatori bassi. Non è una chiamata C:
   non esistono return address, shadow space o prologo Microsoft x64.

L'header espone il simbolo d'ingresso come indirizzo, non come funzione C
invocabile. I layout C verificano dimensioni e offset del record, frame e
GDTR privata. Nel COFF l'unico riferimento esterno è la continuazione.
Il salto finale ha una normale relocation REL32 interna all'immagine:
solo l'avviso NASM su quella relocation è disabilitato localmente; LINK
risolve e controlla il collegamento all'indirizzo alto con /WX e /NODEFAULTLIB.

## Ownership e failure

Il frame usa lo stack già previsto: nessuna nuova pagina o lease. Resta
vivo finché IDT e handler iniziali lo referenziano. La continuazione deve
preservare R15, installare il percorso panic/IST definitivo e validare il
contesto prima di chiamare C o costruire un IRETQ. Non può rilasciare il
frame o dichiarare PARKED soltanto perché il prefisso è terminato.

La fase 1/2 del frame e il terminale 0xffffffff sono diagnostica locale,
non pubblicazioni atomiche del coordinatore Rust e non indicano CPU online.
Gli handler iniziali usano IST=0, scrivono fase/errore nel frame e arrestano
la CPU. Non sono recovery da stack corrotto, gestione completa del double
fault o panic SMP. Una failure precedente al frame si arresta senza usare
GS o chiamare C. Il BSP dovrà riconoscerla tramite la propria deadline e
quarantena; quel collegamento rimane da implementare.

Restano invariati PAT/MTRR, IF=0 e blocco NMI. FNINIT marca vuoti i registri
x87 e ne inizializza i controlli; non certifica la cancellazione dei loro
payload. Nessun user mode o modello SIMD esteso viene introdotto.

## Fonti hardware verificate

- [Intel SDM Vol.3A 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
  §§2.4, 3.4.5, 4.5, 6.7.1, 6.14.1, 8.2.4/8.7, 10.2.1 e 14.1.3,
  per descrittori, CR3, NMI, gate, TSS e inizializzazione x87/SSE.
- [Intel SDM Vol.4 335592-078US, tabella 2-2](https://cdrdv2-public.intel.com/671098/335592-sdm-vol-4.pdf):
  IA32_GS_BASE, C0000101H, prerequisito long mode, base canonica R/W.
- [QEMU gdbstub, memoria fisica](https://www.qemu.org/docs/master/system/gdb.html#examining-physical-memory):
  il test alterna esplicitamente memoria fisica e virtuale e verifica le
  risposte, per leggere le pagine basse dopo la loro rimozione dal mapping.

## Prove e limiti

L'harness include lo stesso sorgente Assembly del prefisso, con una
continuazione diagnostica separata. Usa pacchetti prodotti dal builder C/Rust
reale e pagine di test, non l'allocatore e le lease del kernel. Due pagine
dello stack, descrittori e area GS sono predisposti dall'host; il test non
prova la geometria completa delle risorse AP, gli IST o il bootstrap UEFI.

Campagna finale e manifest riportati in [k3i-protocol.md](k3i-protocol.md).
Ogni positivo verifica record copiato, CR3 operativo senza metà bassa,
GDT/IDT alte, TSS busy, GS, x87/MXCSR/XMM e NMI ancora bloccato.
La fixture altera prima controlli x87/MXCSR e riempie tutti gli XMM con
bit a uno: il riscontro finale non si limita allo stato lasciato da QEMU.
I negativi provocano accesso ai vecchi dati bassi, lettura di guardia e
TSS invalida: richiedono il fault CPU corrispondente e arresto iniziale,
distinti dai crash inattesi. Il test guardia non è uno stack overflow.

Non si chiude il gate K3-I: nessun AP avviato, nessuna prova INIT/SIPI,
kernel UEFI finale, cache dopo INIT, IRETQ definitivo o panic remoto.
ASan/Valgrind non misurano questo Assembly bare metal; il solo test C
contiene assert di layout e una stampa, senza gestione di memoria dinamica.
Nessuna deduzione su leak fisici, coverage o hardware reale.
