# Input PS/2 — validazione

I report completi precedenti sono in `kernel/drivers/input/ps2/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

I checkpoint controller e decoder verificano timeout, ACK/RESEND,
rollback, bounds e ripristino degli owner. La riorganizzazione
riporta 24 test Rust debug/release, suite C/ASan e 10.000 cicli
con controller modellato. QEMU prova dieci boot input e dieci
ordinari di regressione. Lo stress citato usa 612 pressioni.
Il percorso ordinario di regressione non comprende input PS/2.

Host con `./kernel/drivers/input/ps2/scripts/test.ps1`.
I casi controller e regressione usano il runner omonimo
`firmware.ps1` con `-Controller` o `-Ordinary`.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
ps2-decoder-1e53b635f8974c1186f32310d51db0c9
ps2-decoder-81b584690cc24e9e855eda24a7b2d5c3
ps2-decoder-9f014de0775a460eb70a47d3cb812860
ps2-decoder-bf6569b9e0764d8cae9232ff8e330096
ps2-decoder-firmware-577ea4f924304c4594bd3a98f4e1da9a
ps2-decoder-firmware-5fbe62cdbcc84a47a2b2e1412311f53c
ps2-decoder-firmware-86509b5e7fb74db6a445e975dc056b95
ps2-decoder-firmware-902011b99fb74cbe9565652c9e3bef84
ps2-decoder-firmware-b18b7aa95aa547cb9906e314ae9e047b
ps2-decoder-firmware-c39639d14ea740b496e4eb7b46081f2d
ps2-input-firmware-60936e58bf12496fabc54767f52a4c74
ps2-input-firmware-61af231647bc4c979c37f3fd86abdbfd
ps2-input-firmware-d604f35ac99b47e791e7359e4ba8dc27
```

</details>
