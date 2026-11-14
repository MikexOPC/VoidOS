# Notifiche IRQ dello scheduler — design

I documenti completi precedenti sono in `kernel/scheduler/irq/documentation/` al commit `49f8cef`.

Il backend lega le notifiche IRQ al dispatcher con token e contesto
validati. L'entry salva lo stato e il backend hardware ferma o
riconosce la sorgente ed esegue l'EOI. Il confine comune registra
l'evento pending. Il dispatcher drena i pending e applica i
risvegli prima della selezione, evitando mutazioni arbitrarie
della coda pronta dall'handler.

L'owner del binding resta vivo finché sorgente e subscription
sono attive. Token stantio, contesto errato o teardown incerto
vengono rifiutati. Il binding del percorso ordinario serve il
runtime sul BSP. Le iniezioni di errori e la preemption AP sono
collaudate nei rispettivi profili separati.

[Validazione del modulo](validation.md).
