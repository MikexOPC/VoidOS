# Notifiche IRQ dello scheduler — validazione

I report completi precedenti sono in `kernel/scheduler/irq/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

I report storici distinguono test host di binding, token e frame
con hardware modellato da IRQ reali in QEMU. I casi di teardown
verificano che un owner incerto resti trattenuto. L'assenza di
IRQ inattesi è limitata alle immagini e alle finestre provate.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
lapic-asan-b58c087d080243c7b4e5b441c72766a4
loop-firmware-9d203e6a552541d794de55ed34c057b0
loop-firmware-f459778f45f54a9abfdd8a42f40e67dd
scheduler-context-2b5814819f614d0fad853c96e30584be
scheduler-context-6aa4d362f8204d81a0df2a2ebe671252
scheduler-firmware-0796e8c284fa4184a594a4e613f3a2ad
scheduler-firmware-95cd2a55fe2b46dcb95580a593a33735
scheduler-firmware-a5c7d6298eb340c888bad9379a7926bf
scheduler-firmware-aa16285e6cfe4293bb68f144912eeeb5
scheduler-firmware-b8f38c4581ff4e1d8091ecb2d0ec2d6b
scheduler-firmware-cb4fcbed551a4651b5255a0af675f754
scheduler-firmware-cd22ea4d523c418f9f35b31d261c4cd7
scheduler-firmware-da1d5717597c45a2911600e8ccc73a0e
scheduler-firmware-e7ac2ead79474829b4d582cdf4096978
scheduler-host-0722ec972a4b44f7811454ead558d754
scheduler-host-e8c1b5496b79492bb0386857714b739f
```

</details>
