# Watchdog HPET

Il watchdog fornisce un risveglio indipendente al BSP quando
l'IRQ LAPIC atteso non arriva. Il [contratto fra moduli](../../../../../../../docs/design/consolidamento.md#descrizione-hpet-e-watchdog)
e la [validazione](../../../../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86)
distinguono boot ordinario e profili di prova. Il design precedente
e i report completi sono in questa cartella al commit `8bad38a`.

## Owner e apertura

`X86_WATCHDOG_ENV` lega descrizione HPET, contesto di boot,
inventario paging, window, profilo APIC e mapping LAPIC.
`X86_HPET_WATCHDOG` conserva risorse acquisite, token IRQ,
comparatore, route, configurazione da ripristinare, contatori,
esito e fase di errore. Il modulo opera sul BSP. Verifica
ambiente, capacità e proprietà prima di armare. Cerca una
coppia comparatore/pin tra quelle annunciate e ammesse,
senza sovrapporsi a una route già posseduta. Prepara mapping
UC, route level e handler, poi verifica l'apertura prima
dell'unmask. Gli errori di apertura non pubblicano un owner pronto.

## Armamento, IRQ e attesa

1. `wait_private` verifica owner pronto, token vivo, sorgente
   valida e assenza di armamento precedente. Calcola 100 ms per
   l'attesa della sorgente e 5 ms per l'idle.
2. Il backend ferma il contatore globale per programmare il
   confronto, verifica stato pendente e readback e arma il
   one-shot a 32 bit. Pubblica Armed prima della riabilitazione.
   La policy controlla che la scadenza sia ancora futura.
3. La finestra Assembly dorme con interrupt abilitati e budget
   finito. L'attesa verifica la consegna del watchdog anche se
   LAPIC o input hanno già progredito, così il cleanup non
   lascia un IRQ o un handler ancora in uso.
4. L'handler tratta lo stato W1C del comparatore posseduto e
   completa EOI. Il one-shot a 32 bit può generare un evento
   anche al rollover. La policy distingue quell'evento dalla
   scadenza attesa e conserva l'armamento valido.
5. Al ritorno verifica contatore, disarmo, stato IRQ e window.
   Classifica EVENT, TIMEOUT, FAULT o STOP. Una scadenza durante
   l'idle diventa IDLE, mentre la stessa scadenza con una
   sorgente attesa produce la diagnostica DEADLINE.
6. Match saltato. Se i 256 risvegli finiscono senza IRQ del
   watchdog, il backend legge l'HPET. Contatore oltre l'inizio di
   almeno due volte la durata e stato a zero: ferma il contatore,
   rilegge lo stato, disarma come l'handler, avanza il contatore
   del watchdog, stampa `KERNEL_WATCHDOG_MISSED_MATCH` e conta
   l'evento (`KERNEL_WATCHDOG_MISSED` alla chiusura). Stato
   presente, prima o dopo l'arresto: l'IRQ è vero e l'attesa
   dorme di nuovo per riceverlo. Contatore ancora vicino
   all'inizio: raffica di risvegli, errore WAKE come prima.
   Osservato con QEMU 11.1 (TCG) circa una volta su un milione
   di armamenti. Il rilevamento richiede altri risvegli: con il
   tick di preemption arriva dopo circa 2,6 s; senza, solo al
   giro del contatore a 32 bit (circa 43 s a 100 MHz).

## Chiusura e recovery

La chiusura verifica quiescenza, maschera la route, revoca
la sottoscrizione e ripristina dispositivo e IOAPIC prima
di liberare il mapping. Il primo errore conserva fase e
dettaglio per la diagnosi. La policy di recovery distingue
stop richiesto, retry ammissibile, risorse trattenute, rilascio
e budget esaurito. Il retry è delimitato da
`X86_WATCHDOG_RECOVERY_LIMIT` (3). Se stop, readback o restore
non confermano uno stato sicuro, la risorsa rimane all'owner.
Il modello presuppone HPET, controller e BSP funzionanti. Le
perdite LAPIC, la route alternativa e le iniezioni di recovery
si provano con configurazioni dedicate.

## Registri e fonti

| Registro | Uso |
| --- | --- |
| HPET 0x00/0x04 | ID e periodo. NUM_TIM_CAP[12:8] limita la scansione. |
| HPET 0x10 | Enable bit 0, Legacy bit 1. Programmazione a contatore fermo, senza legacy replacement. |
| HPET 0x20 | Stato IRQ W1C. Si scrive solo il bit del timer posseduto. |
| HPET 0xf0/0xf4 | Contatore principale. Il confronto usa aritmetica modulo 32 bit. |
| HPET 0x100 + 0x20*n e +4 | Configurazione comparatore e bitmap delle route. Selezione level, one-shot e route ammessa. |
| HPET 0x108 + 0x20*n | Comparatore, con readback prima del riavvio. |
| IOAPIC 0x00/0x10 | Selector e finestra dword. Redirection 0x10 + 2*pin. Mask, trigger e destinazione sono verificati. |

Riferimenti Intel HPET 1.0a §§2.3.4–2.3.9, §2.4.3.1 per il
rollover e §2.4.6 per lo stato IRQ, Intel 82093AA §§3.1–3.2
per IOAPIC e Intel SDM Vol. 3A §12.5.4 per il timer LAPIC.
I dettagli di capability, bit riservati e readback sono nei
sorgenti. Le prove del progetto usano QEMU/OVMF.
