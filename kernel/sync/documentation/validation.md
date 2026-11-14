# Sincronizzazione — validazione

I report completi precedenti sono in `kernel/sync/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

La prima validazione registra 140.000 acquisizioni FIFO host su
gruppi da 2, 4 e 8 thread, più sette arresti attesi. QEMU 10/10
diagnostici, 10/10 ordinari e due negativi. La chiusura K2 registra
17/17 smoke e dieci boot aggiuntivi dell'ELF heap ricompilato.
La contesa host con ASan non prova concorrenza del guest di quel
checkpoint, che esegue il runtime sul BSP.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
kernel-heap-20d7e7d570dd467c90c3bba3938a3c0a
lapic-asan-ec23ae7e439d468ea415c3b3f7fa6add
qemu-run-124953b8355542c9bad92ea200f54112
qemu-run-1475075e3dd64d6a973e89d362567698
qemu-run-add5c6237ada4811bef77de77e128591
qemu-run-ca089f62314e48a999c39e68d38a9800
sync-firmware-08b09deaae29459ca2ec81beba1a9b51
sync-firmware-0e64e7ef8e2f4a2c846bf2e7610da199
sync-firmware-2fc59a4089c3424593eef27be97b5b10
sync-firmware-736b95679e24457fb4af7ff8f67c2cfe
sync-firmware-8abf6d374cae471988ff2b9b7f63d3b5
sync-firmware-93aa6447a94d4618aea7f8fd4b08a795
sync-firmware-acc10d6822534eb2aebffe4b1c439534
sync-host-73712d2efbf64739ae8b3c9f8259a19b
test-run-06e6010c98344fdda1ebaa831ae37ac3
test-run-1b95fb4e358b41f4b1202e5d8ddd47d5
test-run-679c9554be4e488e988ede1300f3a6b6
test-run-a002cbee316c47c4ae64cb7f15e40c66
```

</details>
