# K3-I — ammissione del contesto AP

1 novembre 2026. Estensione esecutiva del [design approvato](k3i-design.md).
Avvio sequenziale, diagnostico dedicato e arresto diagnostico dopo la
barriera restano acquisiti. Questo documento non attesta AP avviati.

## Riscontro sulle interfacce attuali

- percpu.c conserva le aree private ma espone soltanto un current BSP e
  getter PREPARED riservati al BSP. Non esiste una promozione AP nativa.
- clock/binding.c ha descrittori PM per ciascun AP; validazione e current
  dipendono ancora dal BSP. Il callback di lettura non dipende da GS.
- identity/registry.c rende accessibile il registro solo dal BSP.
- Il provider architetturale dei lock rifiuta ogni CPU non BSP. La sua
  fase iniziale non è una pubblicazione atomica utilizzabile dagli AP.
- xp_panic_native_prepare pubblica con release xp_panic_armed; l'admit
  acquisisce prima di leggere ID, root, PAT, contesti e IDT predisposti.
  Richiede però ACTIVE e IDTR finale già installato sul core chiamante.
- Il gruppo joint SEALED trattiene le risorse. Il relativo registro è
  BSP-only; non può diventare implicitamente una primitiva SMP.

Non usare i preflight che richiedono arena zero dopo l'uso dello stack o
l'inizializzazione dei record panic. Servono verifiche strutturali e della
durata, distinte dai controlli iniziali del contenuto.

## Alternativa A: pubblicazione unica del gruppo

Il BSP costruisce un descrittore stabile con count, root/PAT, identità,
aree locali e descrittori wait attesi. L'oggetto non contiene copie dei
lock mutabili né puntatori alle variabili locali del bootstrap.
Dopo joint SEALED e preparazione panic, il BSP pubblica una sola volta
con release. Ogni AP acquisisce prima di accedere ai campi condivisi.
Nessuna allocazione o modifica al descrittore dopo la pubblicazione.

Il descrittore rende i dati leggibili, non autorizza l'esecuzione degli
AP: token ARMED, claim, identità hardware e verifiche locali restano
obbligatori. Nessun AP online sulla base della sola pubblicazione.

L'AP identifica l'area fra gli indirizzi conosciuti senza dereferenziare
un GS arbitrario. Ricontrolla CPUID/APIC, BSP flag, GS/self/indice, root,
PAT, GDT/TR/TSS, stack/frame e wait privato. I dati numerici del record
sono confrontati con la descrizione posseduta dal kernel, non usati come
autorità autonoma. Soltanto quel core può modificare i propri lock.

Il provider AP resta chiuso fino al completamento dell'ammissione. Il
bootstrap BSP conserva il suo percorso iniziale: nessuna lettura della
vecchia fase non atomica da parte di AP. La politica del timer PM e i
limiti dei lock rimangono quelli già concordati.

Il campo State attuale non deve essere usato come mailbox SMP mediante
letture/scritture C ordinarie. L'ammissione osservabile da altri core
richiede uno stato atomico separato o accessi atomici coerenti con il suo
ABI; non mescolare accessi C non atomici concorrenti. Il BSP non legge i
frame lock di un AP attivo e non riutilizza i getter PREPARED dopo l'avvio.

Sequenza concettuale, non codice eseguibile:

    BSP: seal -> panic prepare -> descrittore release -> token/start
    AP: acquire -> claim -> verifica contesto -> promozione locale
        -> IDT finale -> panic admit -> piano -> IRETQ
        -> conferma PRIVATE/PARKED senza riferimenti alla memoria bassa

Durante promozione/IDT/admit gli NMI restano bloccati dal protocollo
approvato. Ogni errore trattiene tutte le risorse e termina la CPU;
il BSP impone deadline e quarantena. Il fallimento dopo ACTIVE non
riporta il core a PREPARED e non consente un nuovo tentativo.

## Alternativa B: pubblicazione distinta per AP

Un descrittore e una pubblicazione per ciascun core permettono preparazione
e riarmo indipendenti. Richiedono generazioni e lifetime per ogni oggetto,
oltre a regole per modifica/rimozione. Sono utili per hotplug o startup
parallelo, entrambi fuori dalla v0 sequenziale già scelta.

A è la scelta acquisita dall'utente il 1 novembre: descrittore unico
immutabile e nessuna API hotplug. Non occorre riaprire questa decisione.
Perimetro: per-CPU/clock/identità e soli adattatori del provider contesto,
panic, bootstrap, build e test necessari; nessun scheduler SMP o driver.

## Fonti e accettazione

Il protocollo release/acquire deriva dal contratto Rust delle atomiche
([Ordering](https://doc.rust-lang.org/core/sync/atomic/enum.Ordering.html))
e dal design locale approvato. IF=0 non sostituisce la sincronizzazione.
Per IDTR, TSS, GS e NMI restano le sezioni hardware e le prove documentate
in [ritorno](k3i-return-validation.md) e [ingresso](k3i-private-entry.md).
Prima di nuovi accessi privilegiati verificarne nuovamente prerequisiti,
offset e fonte primaria. Nessun nuovo accesso hardware in questo documento.

Accettazione prevista: negativi per pubblicazione assente/duplicata,
CPU sconosciuta, GS non appartenente al gruppo, token stale, contesto o
wait alterati; output/stati invariati sulle failure reversibili, nessun
rilascio dopo la barriera. Test concorrenti per la pubblicazione, host
ASan sul C e audit dei dati immutabili/mutabili. Il completamento runtime
richiede dieci boot UEFI con AP realmente osservati, negativi terminali,
panic remoto e regressioni BSP. Una fixture che scrive ACTIVE non basta.
