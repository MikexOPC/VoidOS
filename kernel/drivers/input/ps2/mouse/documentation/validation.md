# Mouse PS/2 — validazione

I report completi precedenti sono in `kernel/drivers/input/ps2/mouse/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

Il checkpoint runtime registra sette casi Rust debug/release,
2.097.152 pacchetti ABI e 169.985 controlli di trasporto. Il
consumer è provato per perdita, pacchetto parziale e overflow.
QEMU verifica route, EOI, input concorrente e cleanup con
immagini distinte. I dati completi delle prove controller,
resync e runtime sono nei report storici indicati sopra.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
pointer-interactive-d569c27f35bd4185bf4327079c2a03b5
ps2-decoder-441ed3606d844396a4a7c867f83bc3b7
ps2-decoder-90d8e5530fbb455ca9fc95267b1834a0
ps2-decoder-977638367655489887dfa09df876af28
ps2-decoder-firmware-278f234e36b64e57a42220af3c589845
ps2-input-firmware-0c1326ec3c284c3fbe24f52b99a80b57
ps2-input-firmware-1820dfc194e1406fae2e6802f38cfeae
ps2-input-firmware-2f0cc449ba0540a487e1831de2a80d5a
ps2-input-firmware-3992e13b60714bc0aa5994f0142b0b11
ps2-input-firmware-48b972b3f46c4f9e94ead594c7a4dc46
ps2-input-firmware-4c3d2bbbf3684bd38cc16131609cacc7
ps2-input-firmware-4e0b2c8ae3b14fbca3d781dba01d5338
ps2-input-firmware-69ecfd344ca2467995800aca12e43433
ps2-input-firmware-a6dc1112a2934ecd80c3fd7b99438c36
ps2-input-firmware-b8394dd442e64bc19898674c2b1ff027
ps2-mouse-7d86e0b16f9f45808b667ae8c8d8191a
ps2-mouse-task-after-15f6b08d6de545419ddfabe7988f775c
scheduler-context-8a1ccc496ed14ab5af40473eb9fbf187
scheduler-firmware-932dde47493d4b3c858a322e7b430c0d
scheduler-firmware-9374eca0838e4fcc88db102d46a873e4
scheduler-firmware-a7a98b8bfa594e349e3cd96fd778dab2
```

</details>
